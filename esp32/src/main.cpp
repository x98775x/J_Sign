#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>

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

HardwareSerial SerialGD32(1);  // UART1
String gd32Buffer = "";

// ============================================================
// WLED JSON HTTP API - one POST per call, same shapes as before
// ============================================================
bool wledPost(const String& jsonBody) {
  HTTPClient http;
  String url = String("http://") + WLED_IP + "/json/state";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(jsonBody);
  http.end();
  if (code != 200) {
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
    char c = SerialGD32.read();
    if (c == '\n') {
      handleGD32Command(gd32Buffer);
      gd32Buffer = "";
    } else if (c != '\r') {
      gd32Buffer += c;
    }
  }
  // Encoder/knob lives on GD32 side - GD32 sends commands here
}
