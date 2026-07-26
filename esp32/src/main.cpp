#include <Arduino.h>
#include <ArduinoJson.h>

// ============================================================
// ESP32 DevKitC V4 Bridge Firmware
// Replaces Pico W as the bridge MCU between GD32 and WLED
//
// UART assignments:
//   Serial  (UART0) = USB debug, GPIO1(TX)/GPIO3(RX) - default, don't change
//   Serial1 (UART1) = GD32F205 mainboard via UART4 header (RX4/TX4/GND)
//                     ESP32 GPIO17(TX) -> GD32 RX4
//                     ESP32 GPIO16(RX) -> GD32 TX4
//   Serial2 (UART2) = WLED ESP8266 board serial
//                     ESP32 GPIO26(TX) -> WLED RX
//                     ESP32 GPIO25(RX) -> WLED TX
//
// Power: 3.3V logic on all UART pins - matches GD32 and WLED
// Power the ESP32 from USB 5V pin or VIN, GND to GND
// ============================================================

#define GD32_RX  16
#define GD32_TX  17
#define WLED_RX  25
#define WLED_TX  26

#define GD32_BAUD  115200
#define WLED_BAUD  115200

// ESP32 Arduino uses HardwareSerial with UART number + pin remapping
HardwareSerial SerialGD32(1);  // UART1
HardwareSerial SerialWLED(2);  // UART2

String gd32Buffer  = "";
String wledBuffer  = "";

// ============================================================
// WLED serial JSON API commands
// ============================================================
void wledSelectPreset(int presetId) {
  StaticJsonDocument<64> doc;
  doc["ps"] = presetId;
  serializeJson(doc, SerialWLED);
  SerialWLED.println();
}

void wledSetPower(bool on) {
  StaticJsonDocument<32> doc;
  doc["on"] = on;
  serializeJson(doc, SerialWLED);
  SerialWLED.println();
}

void wledSetBrightness(uint8_t bri) {
  StaticJsonDocument<32> doc;
  doc["bri"] = bri;
  serializeJson(doc, SerialWLED);
  SerialWLED.println();
}

void wledSetColor(uint8_t r, uint8_t g, uint8_t b) {
  StaticJsonDocument<128> doc;
  JsonArray seg = doc.createNestedArray("seg");
  JsonObject s  = seg.createNestedObject();
  JsonArray col = s.createNestedArray("col");
  JsonArray c0  = col.createNestedArray();
  c0.add(r); c0.add(g); c0.add(b);
  serializeJson(doc, SerialWLED);
  SerialWLED.println();
}

void wledSetEffect(int fx) {
  StaticJsonDocument<64> doc;
  JsonArray seg = doc.createNestedArray("seg");
  JsonObject s  = seg.createNestedObject();
  s["fx"] = fx;
  serializeJson(doc, SerialWLED);
  SerialWLED.println();
}

void wledSetSpeed(uint8_t sx) {
  StaticJsonDocument<64> doc;
  JsonArray seg = doc.createNestedArray("seg");
  JsonObject s  = seg.createNestedObject();
  s["sx"] = sx;
  serializeJson(doc, SerialWLED);
  SerialWLED.println();
}

void wledSetIntensity(uint8_t ix) {
  StaticJsonDocument<64> doc;
  JsonArray seg = doc.createNestedArray("seg");
  JsonObject s  = seg.createNestedObject();
  s["ix"] = ix;
  serializeJson(doc, SerialWLED);
  SerialWLED.println();
}

void wledSavePreset(int slot) {
  StaticJsonDocument<64> doc;
  doc["psave"] = slot;
  serializeJson(doc, SerialWLED);
  SerialWLED.println();
}

// ============================================================
// Parse commands from GD32
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
  }
}

// ============================================================
// Setup
// ============================================================
void setup() {
  // USB debug serial
  Serial.begin(115200);

  // UART1 -> GD32 mainboard
  // begin(baud, config, rxPin, txPin)
  SerialGD32.begin(GD32_BAUD, SERIAL_8N1, GD32_RX, GD32_TX);

  // UART2 -> WLED ESP8266
  SerialWLED.begin(WLED_BAUD, SERIAL_8N1, WLED_RX, WLED_TX);

  gd32Buffer.reserve(64);
  wledBuffer.reserve(128);

  Serial.println("[Bridge] ESP32 bridge ready");
}

// ============================================================
// Loop
// ============================================================
void loop() {
  // Read from GD32, translate to WLED
  while (SerialGD32.available()) {
    char c = SerialGD32.read();
    if (c == '\n') {
      handleGD32Command(gd32Buffer);
      gd32Buffer = "";
    } else if (c != '\r') {
      gd32Buffer += c;
    }
  }

  // Read WLED responses, log to USB serial
  while (SerialWLED.available()) {
    char c = SerialWLED.read();
    if (c == '\n') {
      Serial.print("[WLED] ");
      Serial.println(wledBuffer);
      wledBuffer = "";
    } else if (c != '\r') {
      wledBuffer += c;
    }
  }

  // Encoder/knob lives on GD32 side - GD32 sends commands here
}
