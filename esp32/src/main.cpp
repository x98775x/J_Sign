#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "AudioFileSource.h"
#include "AudioFileSourceBuffer.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

/* GD32 TFT35 onboard SD -> UART2-wire ESP32 bridge -> MP3 decode -> NS4168.
 * GD32 UART PA2 TX -> ESP32 GPIO16 RX; GD32 PA3 RX <- ESP32 GPIO17 TX.
 * Both UARTs 460800, common ground. Card stays in TFT35's slot.
 */
#define GD32_RX 16
#define GD32_TX 17
#define GD32_BAUD 460800
#define AMP_BCLK 27
#define AMP_LRCLK 26
#define AMP_DIN 25
#define AUDIO_MAX_GAIN 0.8f
const char *AP_SSID="ledsign-bridge";
const char *AP_PASS="change-me-1234";
const char *WLED_IP="192.168.4.2";
HardwareSerial SerialGD32(1);

static constexpr uint32_t RING_SIZE=32768;
static uint8_t ring[RING_SIZE];
static volatile uint32_t ring_head=0,ring_tail=0;
static portMUX_TYPE audio_mux=portMUX_INITIALIZER_UNLOCKED;
static volatile bool src_eof=false,src_open=false,src_playing=false;
static volatile uint32_t src_size=0,src_pos=0,src_generation=0;
static volatile int a_vol=60;
static volatile bool a_volDirty=true;
static volatile int a_cmd=0; // 1 play, 2 stop, 3 next, 4 prev
static volatile uint32_t a_seq=0;
static char a_name[64]="";
static bool sdOk=true;
static bool request_pending=false;
static unsigned long request_ms=0;
static AudioOutputI2S *aout=nullptr;
static AudioFileSourceBuffer *abuf=nullptr;
static AudioGeneratorMP3 *agen=nullptr;

static uint32_t bytes_ready(){
  portENTER_CRITICAL(&audio_mux);
  uint32_t n=ring_head-ring_tail;
  portEXIT_CRITICAL(&audio_mux);return n;
}
static void reset_stream(uint32_t length,const String &name){
  portENTER_CRITICAL(&audio_mux);
  ring_head=ring_tail=0;src_pos=0;src_size=length;src_eof=false;
  src_open=true;src_playing=true;src_generation++;
  strncpy(a_name,name.c_str(),sizeof(a_name)-1);a_name[sizeof(a_name)-1]=0;a_seq++;
  portEXIT_CRITICAL(&audio_mux);
  request_pending=false;
}
static void add_data(const char *hex){
  size_t chars=strlen(hex);
  portENTER_CRITICAL(&audio_mux);
  for(size_t i=0;i+1<chars;i+=2){
    if(ring_head-ring_tail>=RING_SIZE)break;
    auto cv=[](char c)->uint8_t {return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:0;};
    ring[(ring_head++)%RING_SIZE]=(cv(hex[i])<<4)|cv(hex[i+1]);
  }
  portEXIT_CRITICAL(&audio_mux);
  request_pending=false;
}
class UARTAudioSource : public AudioFileSource {
public:
  bool open(const char *) override {return src_open;}
  bool isOpen() override {return src_open;}
  bool close() override {return true;}
  uint32_t getSize() override {return src_size;}
  uint32_t getPos() override {return src_pos;}
  bool seek(int32_t pos,int dir) override { // decoder may seek to start or skip ID3
    uint32_t current=src_pos;
    int64_t newpos=(dir==SEEK_SET)?pos:(dir==SEEK_CUR)?((int64_t)current+pos):((int64_t)src_size+pos);
    if(newpos==(int64_t)current)return true;
    return false; // transport is intentionally sequential
  }
  uint32_t readNonBlock(void *dest,uint32_t length) override {return read(dest,length);}
  uint32_t read(void *dest,uint32_t length) override {
    uint8_t *out=(uint8_t *)dest;
    uint32_t count=0;
    uint32_t generation=src_generation;
    unsigned long deadline=millis()+3500;
    while(count<length){
      if(src_generation!=generation||!src_open||!src_playing)return count;
      portENTER_CRITICAL(&audio_mux);
      uint32_t ready=ring_head-ring_tail;
      uint32_t amount=min((uint32_t)(length-count),ready);
      for(uint32_t i=0;i<amount;i++)out[count+i]=ring[(ring_tail+i)%RING_SIZE];
      ring_tail+=amount;src_pos+=amount;
      portEXIT_CRITICAL(&audio_mux);
      count+=amount;
      if(count==length||src_eof||src_pos>=src_size)break;
      if((long)(millis()-deadline)>=0)break;
      vTaskDelay(1);
    }
    return count;
  }
};
static UARTAudioSource src;
static uint32_t running_generation=0;
static bool a_paused=true;
static void audioTask(void *){
  aout=new AudioOutputI2S(0,AudioOutputI2S::EXTERNAL_I2S);
  aout->SetPinout(AMP_BCLK,AMP_LRCLK,AMP_DIN);
  aout->SetOutputModeMono(true);
  aout->SetGain(AUDIO_MAX_GAIN*(float)a_vol/100.f);
  for(;;){
    if(a_volDirty){a_volDirty=false;aout->SetGain(AUDIO_MAX_GAIN*(float)a_vol/100.f);}
    if(src_generation!=running_generation){
      if(agen){agen->stop();delete agen;agen=nullptr;}
      if(abuf){delete abuf;abuf=nullptr;}
      running_generation=src_generation;
      if(src_open){
        abuf=new AudioFileSourceBuffer(&src,4096);
        agen=new AudioGeneratorMP3();
        a_paused=!agen->begin(abuf,aout);
        src_playing=!a_paused;
        a_seq++;
      }
    }
    int cmd=a_cmd;if(cmd){a_cmd=0;
      if(cmd==1){a_paused=false;src_playing=true;a_seq++;}
      else if(cmd==2){a_paused=true;src_playing=false;a_seq++;}
      else if(cmd==3||cmd==4){a_paused=true;src_playing=false;}
    }
    if(agen&&!a_paused&&agen->isRunning()){
      if(!agen->loop()){
        src_playing=false;a_paused=true;a_seq++;
        if(src_eof||src_pos>=src_size)SerialGD32.println("A:NEXT");
      }
      vTaskDelay(1);
    } else vTaskDelay(8);
  }
}
static void audioBegin(){
  xTaskCreatePinnedToCore(audioTask,"audio",12288,nullptr,2,nullptr,1);
}
static uint32_t lastReportedSeq=0xffffffff;
static void sendTrackAndState(){
  if(a_name[0]){SerialGD32.print("TRACK:");SerialGD32.println(a_name);}
  SerialGD32.println(src_playing?"STATE:PLAY":"STATE:STOP");
  lastReportedSeq=a_seq;
}
static void sendStatus(){
  SerialGD32.println(sdOk?"READY":"NOSD");sendTrackAndState();
}
static void reportAudioChanges(){if(a_seq!=lastReportedSeq)sendTrackAndState();}
static void processAudioLine(const String &line){
  if(line.startsWith("A:DATA:")){add_data(line.c_str()+7);}
  else if(line.startsWith("A:FILE:")){
    int sep=line.indexOf(':',7);
    if(sep>7){uint32_t size=strtoul(line.substring(7,sep).c_str(),nullptr,10);
      reset_stream(size,line.substring(sep+1));sdOk=true;}
  } else if(line=="A:EOF"){src_eof=true;request_pending=false;}
  else if(line=="A:NOSD"){sdOk=false;src_playing=false;src_open=false;src_generation++;request_pending=false;
    a_name[0]=0;a_seq++;SerialGD32.println("NOSD");}
}
static void requestData(){
  if(!src_open||src_eof||!src_playing)return;
  if(request_pending){if(millis()-request_ms<1600)return;request_pending=false;}
  if(bytes_ready()<12500){SerialGD32.println("A:READ");request_pending=true;request_ms=millis();}
}
// ============================================================
// WLED over its JSON HTTP API
// ============================================================
static bool wledUp = false;
static int  wledFails = 0;

bool wledPost(const String &jsonBody) {
  if (WiFi.softAPgetStationNum() == 0) { wledUp = false; return false; }   // WLED not on the AP yet: don't wait on timeouts
  HTTPClient http;
  String url = String("http://") + WLED_IP + "/json/state";
  http.begin(url);
  http.setConnectTimeout(300);
  http.setTimeout(500);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(jsonBody);
  http.end();
  if (code < 200 || code >= 300) {
    Serial.printf("[WLED] POST failed, code=%d body=%s\n", code, jsonBody.c_str());
    if (++wledFails >= 3) wledUp = false;            // lost it - announce again when it returns
    return false;
  }
  wledFails = 0;
  wledUp = true;
  return true;
}

void wledSelectPreset(int presetId) {
  StaticJsonDocument<64> doc;
  doc["ps"] = presetId;
  String out; serializeJson(doc, out);
  wledPost(out);
}
void wledSetPower(bool on) {
  StaticJsonDocument<32> doc;
  doc["on"] = on;
  String out; serializeJson(doc, out);
  wledPost(out);
}
void wledSetBrightness(uint8_t bri) {
  StaticJsonDocument<32> doc;
  doc["bri"] = bri;
  String out; serializeJson(doc, out);
  wledPost(out);
}
void wledSetColor(uint8_t r, uint8_t g, uint8_t b) {
  StaticJsonDocument<128> doc;
  JsonArray seg = doc.createNestedArray("seg");
  JsonObject s  = seg.createNestedObject();
  JsonArray col = s.createNestedArray("col");
  JsonArray c0  = col.createNestedArray();
  c0.add(r); c0.add(g); c0.add(b);
  String out; serializeJson(doc, out);
  wledPost(out);
}
void wledSetEffect(int fx) {
  StaticJsonDocument<64> doc;
  JsonArray seg = doc.createNestedArray("seg");
  JsonObject s  = seg.createNestedObject();
  s["fx"] = fx;
  String out; serializeJson(doc, out);
  wledPost(out);
}
void wledSetSpeed(uint8_t sx) {
  StaticJsonDocument<64> doc;
  JsonArray seg = doc.createNestedArray("seg");
  JsonObject s  = seg.createNestedObject();
  s["sx"] = sx;
  String out; serializeJson(doc, out);
  wledPost(out);
}
void wledSetIntensity(uint8_t ix) {
  StaticJsonDocument<64> doc;
  JsonArray seg = doc.createNestedArray("seg");
  JsonObject s  = seg.createNestedObject();
  s["ix"] = ix;
  String out; serializeJson(doc, out);
  wledPost(out);
}
void wledSavePreset(int slot) {
  StaticJsonDocument<64> doc;
  doc["psave"] = slot;
  String out; serializeJson(doc, out);
  wledPost(out);
}

// Until WLED answers, poll it every few seconds. When it first appears, tell the GD32 so it
// pushes the current preset / brightness / power again (the sign powers up with both at once).
static void wledPoll() {
  static uint32_t lastTry = 0;
  if (wledUp) return;
  if (millis() - lastTry < 4000) return;
  lastTry = millis();
  if (WiFi.softAPgetStationNum() == 0) return;       // nobody has joined the AP yet
  HTTPClient http;
  http.begin(String("http://") + WLED_IP + "/json/info");
  http.setConnectTimeout(300);
  http.setTimeout(500);
  int code = http.GET();
  http.end();
  if (code == 200) {
    wledUp = true;
    wledFails = 0;
    SerialGD32.println("WLED:UP");
    Serial.println("[WLED] online");
  }
}

// ============================================================
// Commands from the GD32 (and from the USB serial monitor for testing)
// ============================================================
void handleCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd == "PLAY") {
    if (!src_open) SerialGD32.println("A:OPEN");
    a_cmd = 1;
  } else if (cmd == "STOP") {
    a_cmd = 2;
  } else if (cmd == "NEXT") {
    a_cmd = 3; SerialGD32.println("A:NEXT");
  } else if (cmd == "PREV") {
    a_cmd = 4; SerialGD32.println("A:PREV");
  } else if (cmd.startsWith("VOL:")) {
    a_vol = constrain((int)cmd.substring(4).toInt(), 0, 100);
    a_volDirty = true;
  } else if (cmd == "STATUS?" || cmd == "TRACKLIST?") {
    sendStatus();
  } else if (cmd.startsWith("PRESET:")) {
    wledSelectPreset(cmd.substring(7).toInt());
  } else if (cmd == "ON") {
    wledSetPower(true);
  } else if (cmd == "OFF") {
    wledSetPower(false);
  } else if (cmd.startsWith("BRI:")) {
    wledSetBrightness((uint8_t)constrain(cmd.substring(4).toInt(), 0, 255));
  } else if (cmd.startsWith("COLOR:")) {
    String vals = cmd.substring(6);
    int c1 = vals.indexOf(',');
    int c2 = vals.lastIndexOf(',');
    if (c1 > 0 && c2 > c1) {
      wledSetColor((uint8_t)vals.substring(0, c1).toInt(),
                   (uint8_t)vals.substring(c1 + 1, c2).toInt(),
                   (uint8_t)vals.substring(c2 + 1).toInt());
    }
  } else if (cmd.startsWith("FX:")) {
    wledSetEffect(cmd.substring(3).toInt());
  } else if (cmd.startsWith("SX:")) {
    wledSetSpeed((uint8_t)constrain(cmd.substring(3).toInt(), 0, 255));
  } else if (cmd.startsWith("IX:")) {
    wledSetIntensity((uint8_t)constrain(cmd.substring(3).toInt(), 0, 255));
  } else if (cmd.startsWith("SAVE:")) {
    wledSavePreset(cmd.substring(5).toInt());
  } else {
    Serial.printf("[CMD] Unknown command: %s\n", cmd.c_str());
  }
}

// ============================================================
// Setup / loop
// ============================================================
static String gd32Buffer;
static bool   gd32Overflow = false;

void setup() {
  Serial.begin(115200);
  SerialGD32.begin(GD32_BAUD, SERIAL_8N1, GD32_RX, GD32_TX);
  gd32Buffer.reserve(850);

  audioBegin();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("[AP] Started, IP: ");
  Serial.println(WiFi.softAPIP());                   // normally 192.168.4.1

  sendStatus();                                      // READY (or NOSD) -> GD32 pushes LED state
  Serial.println("[Bridge] ESP32 bridge ready");
}

void loop() {
  while (SerialGD32.available()) {
    char c = (char)SerialGD32.read();
    if (c == '\n') {
      if (!gd32Overflow) { if (gd32Buffer.startsWith("A:")) processAudioLine(gd32Buffer); else handleCommand(gd32Buffer); }
      gd32Buffer = "";
      gd32Overflow = false;
    } else if (c != '\r' && !gd32Overflow) {
      if (gd32Buffer.length() < 840) gd32Buffer += c;
      else { gd32Buffer = ""; gd32Overflow = true; Serial.println("[GD32] Discarded oversized command"); }
    }
  }

  // USB serial monitor: type PLAY / NEXT / VOL:40 / STATUS? ... for bench testing.
  static String usbBuffer;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') { handleCommand(usbBuffer); usbBuffer = ""; }
    else if (c != '\r' && usbBuffer.length() < 96) usbBuffer += c;
  }

  requestData();
  reportAudioChanges();
  wledPoll();
}
