#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "AudioFileSource.h"
#include "AudioGeneratorMP3.h"
#include "AudioGeneratorWAV.h"
#include "AudioOutputI2S.h"

// ============================================================
// ESP32 DevKitC V4 - bridge + audio decoder for JULIA's Sign
//
//   GD32 screen  <--UART1 @ 1 Mbaud-->  ESP32  --WiFi AP/HTTP-->  WLED
//   (SD card)                             |
//                                         +--I2S--> NS4168 amp --> speaker
//
// The SD card with the music is in the SCREEN. The screen sends the file to us in small
// chunks when we ask for them (READ), we decode MP3/WAV and play it.
//
// Lines GD32 -> ESP32 : PRESET:n ON OFF BRI:n COLOR:r,g,b FX:n SX:n IX:n SAVE:n   (LEDs)
//                       PLAY:<id>:<size>:<mp3|wav>  STOP  PAUSE  RESUME  VOL:n  STATUS?
//                       D:<id>:<offset>:<len>:<sum>\n<len raw bytes>   (answer to READ)
// Lines ESP32 -> GD32 : READY   WLED:UP   READ:<id>:<offset>:<len>   ENDED:<id>
// ============================================================

// ---- UART to the GD32 screen ----
#define GD32_RX    16
#define GD32_TX    17
#define LINK_BAUD  1000000          // must match LINK_BAUD in gd32/src/hal.h

// ---- WiFi / WLED ----
const char* AP_SSID = "ledsign-bridge";
const char* AP_PASS = "change-me-1234";   // min 8 chars, WPA2
const char* WLED_IP = "192.168.4.2";      // WLED's static IP on the AP

// ---- NS4168 I2S amp ----
#define AMP_BCLK   27
#define AMP_LRCLK  26
#define AMP_DIN    25
#define AUDIO_MAX_GAIN 0.8f               // volume 100 = this gain; lower it if the speaker distorts

// ---- streaming buffer ----
#define RING_SIZE  20480                  // ~1 s of a 160 kbps MP3
#define CHUNK      2048                   // bytes per READ

HardwareSerial SerialGD32(1);

// ============================================================
// Shared state (guarded by gLock)
// ============================================================
static SemaphoreHandle_t gLock;
struct Lock {
  Lock()  { xSemaphoreTake(gLock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(gLock); }
};

static uint8_t  ring[RING_SIZE];
static uint32_t ringHead = 0, ringTail = 0;

struct AudioStreamState {
  uint32_t id;
  uint32_t size;
  bool     wav;
  uint32_t rxOffset;      // next byte we need from the screen
  bool     eof;           // everything has been received
  bool     active;
};
static AudioStreamState st = {0, 0, false, 0, false, false};
static bool     reqOut = false;           // a READ is in flight
static uint32_t reqT = 0;

static uint32_t ringAvail() { return (ringHead + RING_SIZE - ringTail) % RING_SIZE; }
static uint32_t ringFree()  { return RING_SIZE - 1 - ringAvail(); }

static void ringPush(const uint8_t *p, uint32_t n) {
  uint32_t room = RING_SIZE - ringHead;
  uint32_t first = (n < room) ? n : room;
  memcpy(ring + ringHead, p, first);
  if (n > first) memcpy(ring, p + first, n - first);
  ringHead = (ringHead + n) % RING_SIZE;
}

static uint32_t ringPop(uint8_t *p, uint32_t n) {
  uint32_t av = ringAvail();
  if (n > av) n = av;
  uint32_t room = RING_SIZE - ringTail;
  uint32_t first = (n < room) ? n : room;
  memcpy(p, ring + ringTail, first);
  if (n > first) memcpy(p + first, ring, n - first);
  ringTail = (ringTail + n) % RING_SIZE;
  return n;
}

// Flags between tasks
static volatile bool     a_start = false, a_stop = false, a_pause = false;
static volatile uint32_t a_endedId = 0;
static volatile int      a_vol = 60;
static volatile bool     a_volDirty = true;
static volatile bool     ev_ready = false, ev_wledUp = false;

// ============================================================
// Audio: a "file" that is really the byte stream coming from the screen
// ============================================================
class StreamSource : public AudioFileSource {
 public:
  StreamSource(uint32_t id, uint32_t size) : myId(id), total(size), pos(0) {}

  uint32_t read(void *data, uint32_t len) {
    uint8_t *p = (uint8_t *)data;
    uint32_t got = 0;
    uint32_t t0 = millis();
    while (got < len) {
      uint32_t n = 0;
      bool gone = false, finished = false;
      {
        Lock l;
        if (st.id != myId) gone = true;
        else {
          n = ringPop(p + got, len - got);
          finished = st.eof && ringAvail() == 0;
        }
      }
      if (gone) break;
      got += n;
      pos += n;
      if (got >= len) break;
      if (n > 0) { t0 = millis(); continue; }
      if (finished) break;                     // true end of the file
      if (millis() - t0 > 1500) break;         // screen went quiet: give up on this song
      vTaskDelay(1);
    }
    return got;
  }

  uint32_t readNonBlock(void *data, uint32_t len) { return read(data, len); }

  bool seek(int32_t off, int dir) {            // forward only (decoders skip tags this way)
    int32_t target;
    if (dir == SEEK_SET)      target = off;
    else if (dir == SEEK_CUR) target = (int32_t)pos + off;
    else                      target = (int32_t)total + off;
    if (target < (int32_t)pos) return false;
    uint8_t scratch[64];
    while ((int32_t)pos < target) {
      uint32_t want = (uint32_t)(target - (int32_t)pos);
      if (want > sizeof(scratch)) want = sizeof(scratch);
      if (read(scratch, want) == 0) return false;
    }
    return true;
  }

  bool close() { return true; }
  bool isOpen() { Lock l; return st.id == myId; }
  uint32_t getSize() { return total; }
  uint32_t getPos() { return pos; }

 private:
  uint32_t myId, total, pos;
};

static AudioOutputI2S *aout = nullptr;
static StreamSource   *asrc = nullptr;
static AudioGenerator *agen = nullptr;
static uint32_t        curId = 0;

static float volToGain(int v) { return AUDIO_MAX_GAIN * (float)constrain(v, 0, 100) / 100.0f; }

static void stopDecoder() {
  if (agen) { agen->stop(); delete agen; agen = nullptr; }
  if (asrc) { delete asrc; asrc = nullptr; }
}

static void startDecoder() {
  uint32_t id, size;
  bool wav, active;
  {
    Lock l;
    id = st.id; size = st.size; wav = st.wav; active = st.active;
  }
  if (!active) return;
  curId = id;
  asrc = new StreamSource(id, size);
  if (wav) agen = new AudioGeneratorWAV();
  else     agen = new AudioGeneratorMP3();
  if (!agen->begin(asrc, aout)) {                   // unreadable: tell the screen so it skips ahead
    stopDecoder();
    a_endedId = id;
  }
}

static void audioTask(void *) {
  for (;;) {
    if (a_stop)  { a_stop = false; stopDecoder(); }
    if (a_start) { a_start = false; stopDecoder(); startDecoder(); }
    if (a_volDirty) { a_volDirty = false; if (aout) aout->SetGain(volToGain(a_vol)); }

    if (agen && agen->isRunning()) {
      if (!a_pause) {
        if (!agen->loop()) {                        // song finished
          uint32_t id = curId;
          stopDecoder();
          a_endedId = id;
        }
      }
      vTaskDelay(a_pause ? 10 : 1);
    } else {
      vTaskDelay(10);
    }
  }
}

// ============================================================
// WLED over its JSON HTTP API
// ============================================================
static bool wledUp = false;
static int  wledFails = 0;

bool wledPost(const String &jsonBody) {
  if (WiFi.softAPgetStationNum() == 0) { wledUp = false; return false; }   // WLED not on the AP yet
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

// Until WLED answers, poll it every few seconds; when it first appears, tell the screen so it
// pushes the current preset / brightness / power again.
static void wledPoll() {
  static uint32_t lastTry = 0;
  if (wledUp) return;
  if (millis() - lastTry < 4000) return;
  lastTry = millis();
  if (WiFi.softAPgetStationNum() == 0) return;
  HTTPClient http;
  http.begin(String("http://") + WLED_IP + "/json/info");
  http.setConnectTimeout(300);
  http.setTimeout(500);
  int code = http.GET();
  http.end();
  if (code == 200) {
    wledUp = true;
    wledFails = 0;
    ev_wledUp = true;
    Serial.println("[WLED] online");
  }
}

// ---- LED commands are queued (latest per type wins) and executed by loop(), because the HTTP calls
//      can take hundreds of ms and must never stall the link task that feeds the music ----
enum { L_PRESET, L_FX, L_SX, L_IX, L_COLOR, L_BRI, L_POWER, L_SAVE, L_COUNT };
static char       ledPend[L_COUNT][40];
static bool       ledHas[L_COUNT];
static portMUX_TYPE ledMux = portMUX_INITIALIZER_UNLOCKED;

static void queueLed(const char *cmd) {
  int k = -1;
  if (strncmp(cmd, "PRESET:", 7) == 0)      k = L_PRESET;
  else if (strncmp(cmd, "FX:", 3) == 0)     k = L_FX;
  else if (strncmp(cmd, "SX:", 3) == 0)     k = L_SX;
  else if (strncmp(cmd, "IX:", 3) == 0)     k = L_IX;
  else if (strncmp(cmd, "COLOR:", 6) == 0)  k = L_COLOR;
  else if (strncmp(cmd, "BRI:", 4) == 0)    k = L_BRI;
  else if (strcmp(cmd, "ON") == 0 || strcmp(cmd, "OFF") == 0) k = L_POWER;
  else if (strncmp(cmd, "SAVE:", 5) == 0)   k = L_SAVE;
  if (k < 0) { Serial.printf("[CMD] Unknown command: %s\n", cmd); return; }
  portENTER_CRITICAL(&ledMux);
  strncpy(ledPend[k], cmd, sizeof(ledPend[k]) - 1);
  ledPend[k][sizeof(ledPend[k]) - 1] = '\0';
  ledHas[k] = true;
  portEXIT_CRITICAL(&ledMux);
}

static void runLedCommand(String cmd) {
  cmd.trim();
  if (cmd.startsWith("PRESET:")) {
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
  }
}

static void runPendingLed() {
  for (int k = 0; k < L_COUNT; k++) {
    char buf[40];
    bool has;
    portENTER_CRITICAL(&ledMux);
    has = ledHas[k];
    if (has) { memcpy(buf, ledPend[k], sizeof(buf)); ledHas[k] = false; }
    portEXIT_CRITICAL(&ledMux);
    if (has) runLedCommand(String(buf));
  }
}

// ============================================================
// Link to the GD32 screen (this task is the ONLY one that touches SerialGD32)
// ============================================================
static void sendLine(const char *s) {
  SerialGD32.print(s);
  SerialGD32.print("\r\n");
}

static void handleLine(const char *l) {
  if (strncmp(l, "PLAY:", 5) == 0) {
    unsigned long id = 0, size = 0;
    char ext[8] = {0};
    if (sscanf(l + 5, "%lu:%lu:%7s", &id, &size, ext) >= 2) {
      {
        Lock lk;
        ringHead = ringTail = 0;
        st.id = (uint32_t)id;
        st.size = (uint32_t)size;
        st.wav = (strcmp(ext, "wav") == 0);
        st.rxOffset = 0;
        st.eof = false;
        st.active = true;
        reqOut = false;
      }
      a_pause = false;
      a_start = true;
    }
  } else if (strcmp(l, "STOP") == 0) {
    {
      Lock lk;
      st.active = false;
      st.id++;                                      // invalidates anything still in flight
      ringHead = ringTail = 0;
      reqOut = false;
    }
    a_stop = true;
  } else if (strcmp(l, "PAUSE") == 0) {
    a_pause = true;
  } else if (strcmp(l, "RESUME") == 0) {
    a_pause = false;
  } else if (strncmp(l, "VOL:", 4) == 0) {
    a_vol = constrain(atoi(l + 4), 0, 100);
    a_volDirty = true;
  } else if (strcmp(l, "STATUS?") == 0) {
    ev_ready = true;
  } else {
    queueLed(l);
  }
}

// A complete D: frame arrived
static void finishFrame(uint32_t id, uint32_t off, uint32_t len, uint32_t sum, const uint8_t *buf) {
  uint32_t s = 0;
  for (uint32_t i = 0; i < len; i++) s += buf[i];
  s &= 0xFFFFu;
  Lock l;
  if (!st.active || id != st.id) return;            // stale frame from an older song
  reqOut = false;
  if (off != st.rxOffset || s != sum) return;       // damaged or out of order: ask again
  if (len == 0) { st.eof = true; return; }
  if (ringFree() < len) return;
  ringPush(buf, len);
  st.rxOffset += len;
  if (st.rxOffset >= st.size) st.eof = true;
}

static void linkTask(void *) {
  static char    line[96];
  static uint8_t frame[CHUNK];
  int      len = 0;
  bool     inData = false;
  uint32_t dId = 0, dOff = 0, dLen = 0, dSum = 0, dGot = 0, dLast = 0;

  for (;;) {
    while (SerialGD32.available()) {
      if (!inData) {
        int ch = SerialGD32.read();
        if (ch == '\n') {
          line[len] = '\0';
          if (len > 2 && line[0] == 'D' && line[1] == ':') {
            unsigned long a, b, c, d;
            if (sscanf(line + 2, "%lu:%lu:%lu:%lu", &a, &b, &c, &d) == 4 && c <= CHUNK) {
              dId = (uint32_t)a; dOff = (uint32_t)b; dLen = (uint32_t)c; dSum = (uint32_t)d;
              dGot = 0;
              dLast = millis();
              if (dLen == 0) finishFrame(dId, dOff, 0, dSum, frame);
              else           inData = true;
            }
          } else if (len > 0) {
            handleLine(line);
          }
          len = 0;
        } else if (ch != '\r' && len < (int)sizeof(line) - 1) {
          line[len++] = (char)ch;
        }
      } else {
        uint32_t want = dLen - dGot;
        uint32_t av = (uint32_t)SerialGD32.available();
        if (av < want) want = av;
        size_t n = SerialGD32.readBytes(frame + dGot, want);
        dGot += (uint32_t)n;
        dLast = millis();
        if (dGot >= dLen) {
          inData = false;
          finishFrame(dId, dOff, dLen, dSum, frame);
        }
      }
    }

    if (inData && (millis() - dLast) > 300) {       // bytes went missing: drop the frame, ask again
      inData = false;
      Lock l;
      reqOut = false;
    }

    if (ev_ready)  { ev_ready = false;  sendLine("READY"); }
    if (ev_wledUp) { ev_wledUp = false; sendLine("WLED:UP"); }
    uint32_t ended = a_endedId;
    if (ended) {
      a_endedId = 0;
      char b[32];
      snprintf(b, sizeof(b), "ENDED:%lu", (unsigned long)ended);
      sendLine(b);
    }

    // Ask for the next piece of the song whenever the buffer has room.
    bool doReq = false;
    uint32_t rId = 0, rOff = 0;
    {
      Lock l;
      if (st.active && !st.eof && !reqOut && ringFree() >= CHUNK) {
        rId = st.id; rOff = st.rxOffset;
        reqOut = true;
        reqT = millis();
        doReq = true;
      } else if (reqOut && (millis() - reqT) > 1000) {
        reqOut = false;                             // request or answer was lost: retry
      }
    }
    if (doReq) {
      char b[64];
      snprintf(b, sizeof(b), "READ:%lu:%lu:%u", (unsigned long)rId, (unsigned long)rOff, (unsigned)CHUNK);
      sendLine(b);
    }

    vTaskDelay(1);
  }
}

// ============================================================
// Setup / loop
// ============================================================
void setup() {
  Serial.begin(115200);
  gLock = xSemaphoreCreateMutex();

  SerialGD32.setRxBufferSize(8192);
  SerialGD32.begin(LINK_BAUD, SERIAL_8N1, GD32_RX, GD32_TX);

  aout = new AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S);
  aout->SetPinout(AMP_BCLK, AMP_LRCLK, AMP_DIN);
  aout->SetOutputModeMono(true);                    // NS4168 is a mono amp
  aout->SetGain(volToGain(a_vol));

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("[AP] Started, IP: ");
  Serial.println(WiFi.softAPIP());                  // normally 192.168.4.1

  xTaskCreatePinnedToCore(linkTask,  "link",  6144,  nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(audioTask, "audio", 12288, nullptr, 2, nullptr, 1);

  ev_ready = true;                                  // READY -> the screen pushes LED state and volume
  Serial.println("[Bridge] ESP32 bridge ready");
}

void loop() {
  runPendingLed();
  wledPoll();

  // USB serial monitor: type VOL:40, PRESET:3, ON ... for bench testing.
  static String usbBuffer;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') { handleLine(usbBuffer.c_str()); usbBuffer = ""; }
    else if (c != '\r' && usbBuffer.length() < 90) usbBuffer += c;
  }
  delay(2);
}
