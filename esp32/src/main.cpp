#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <math.h>

// ============================================================
// ESP32 DevKitC V4 Bridge Firmware
// Bridge MCU between GD32 (UART) and WLED (WiFi/HTTP JSON API)
//
// The Gledopto 2D-EXMU has NO exposed TX/RX header - its only serial
// access is USB-C, and that's for flashing only (not a TTL UART you
// can jumper to). So WLED is driven over its JSON HTTP API instead,
// on a closed local WiFi AP hosted by this ESP32 - fully offline,
// no router or internet required.
//
// UART assignments (unchanged from before):
//   Serial  (UART0) = USB debug, GPIO1(TX)/GPIO3(RX) - default, don't change
//   Serial1 (UART1) = GD32F205 mainboard via UART4 header (RX4/TX4/GND)
//                     ESP32 GPIO17(TX) -> GD32 RX4
//                     ESP32 GPIO16(RX) -> GD32 TX4
//
// WiFi:
//   ESP32 hosts AP_SSID / AP_PASS below.
//   One-time setup: connect the WLED board to this AP via its own
//   captive portal, and give it the static IP in WLED_IP below.
// ============================================================

#define GD32_RX  16
#define GD32_TX  17
#define GD32_BAUD  115200

const char* AP_SSID = "ledsign-bridge";
const char* AP_PASS = "change-me-1234";   // min 8 chars, WPA2
const char* WLED_IP = "192.168.4.2";      // set this as WLED's static IP

// NS4168 I2S mono amplifier (ESP32 output; no SD playback yet)
// ESP32 GPIO27 -> BCLK, GPIO26 -> LRCLK, GPIO25 -> DIN
// Power amp from regulated 5V and shared GND; speaker only to OUT+ / OUT-.
#define AMP_BCLK 27
#define AMP_LRCLK 26
#define AMP_DIN 25
#define AUDIO_RATE 22050

static bool audioReady = false;
static uint32_t toneRemaining = 0;
static uint32_t tonePhase = 0;

void audioBegin() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = AUDIO_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = 0;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  i2s_pin_config_t pins = {};
  pins.bck_io_num = AMP_BCLK;
  pins.ws_io_num = AMP_LRCLK;
  pins.data_out_num = AMP_DIN;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) {
    Serial.println("[AUDIO] I2S driver install failed");
    return;
  }
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
    Serial.println("[AUDIO] I2S pin setup failed");
    i2s_driver_uninstall(I2S_NUM_0);
    return;
  }
  i2s_zero_dma_buffer(I2S_NUM_0);
  audioReady = true;
  Serial.println("[AUDIO] NS4168 I2S ready: BCLK=27 LRCLK=26 DIN=25");
}

void audioTest() {
  if (!audioReady) return;
  tonePhase = 0;
  toneRemaining = AUDIO_RATE / 2; // 0.5s, 440Hz test tone
  Serial.println("[AUDIO] 440Hz test tone");
}

// Feed one short chunk per loop, avoiding a half-second stall of GD32 commands.
void audioTick() {
  if (!audioReady || !toneRemaining) return;
  int16_t frames[128 * 2]; // standard stereo I2S; same mono sample L/R
  const uint32_t count = toneRemaining < 128 ? toneRemaining : 128;
  for (uint32_t i = 0; i < count; ++i) {
    int16_t sample = (int16_t)(sin(6.28318530718f * 440.0f * tonePhase / AUDIO_RATE) * 4000.0f);
    frames[i * 2] = sample;
    frames[i * 2 + 1] = sample;
    ++tonePhase;
  }
  size_t sent = 0;
  // Short bounded wait, so UART handling remains responsive.
  i2s_write(I2S_NUM_0, frames, count * 4, &sent, pdMS_TO_TICKS(2));
  toneRemaining -= sent / 4;
}

HardwareSerial SerialGD32(1);  // UART1
String gd32Buffer = "";
static bool gd32Overflow = false;

// ============================================================
// WLED JSON HTTP API - one POST per call, same shapes as before
// ============================================================
bool wledPost(const String& jsonBody) {
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
    return false;
  }
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

// ============================================================
// Parse commands from GD32 - identical command set, same as before
// PRESET:n | ON | OFF | BRI:n | COLOR:r,g,b
// FX:n | SX:n | IX:n | SAVE:n
// ============================================================
void handleGD32Command(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd == "AUDIO:TEST") {
    audioTest();
  } else if (cmd == "AUDIO:STOP") {
    toneRemaining = 0;
    if (audioReady) i2s_zero_dma_buffer(I2S_NUM_0);
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
      uint8_t r = (uint8_t)vals.substring(0, c1).toInt();
      uint8_t g = (uint8_t)vals.substring(c1+1, c2).toInt();
      uint8_t b = (uint8_t)vals.substring(c2+1).toInt();
      wledSetColor(r, g, b);
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
    Serial.printf("[GD32] Unknown command: %s\n", cmd.c_str());
  }
}

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(115200);

  // UART1 -> GD32 mainboard (unchanged)
  SerialGD32.begin(GD32_BAUD, SERIAL_8N1, GD32_RX, GD32_TX);
  gd32Buffer.reserve(64);
  audioBegin();

  // Host the closed local AP for the WLED board to join
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("[AP] Started, IP: ");
  Serial.println(WiFi.softAPIP());   // normally 192.168.4.1
  Serial.println("[AP] Waiting for WLED board to connect...");

  Serial.println("[Bridge] ESP32 bridge ready");
}

// ============================================================
// Loop
// ============================================================
void loop() {
  while (SerialGD32.available()) {
    char c = (char)SerialGD32.read();
    if (c == '\n') {
      if (!gd32Overflow) handleGD32Command(gd32Buffer);
      gd32Buffer = "";
      gd32Overflow = false;
    } else if (c != '\r' && !gd32Overflow) {
      if (gd32Buffer.length() < 96) gd32Buffer += c;
      else { gd32Buffer = ""; gd32Overflow = true; Serial.println("[GD32] Discarded oversized command"); }
    }
  }
  // USB serial terminal: type AUDIO:TEST then Enter for speaker test.
  static String usbBuffer;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') { handleGD32Command(usbBuffer); usbBuffer = ""; }
    else if (c != '\r' && usbBuffer.length() < 96) usbBuffer += c;
  }
  audioTick();
  // Encoder/knob lives on GD32 side - GD32 sends commands here
}
