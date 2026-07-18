#include <Arduino.h>
#include <ArduinoJson.h>

// ---- UART assignments ----
// Serial1 (UART0) -> GD32F205 mainboard, via its UART4 header (RX4/TX4/GND)
// Serial2 (UART1) -> WLED ESP8266 board, via its serial pins
//
// Default Arduino-Pico pins:
//   Serial1: TX=GPIO0, RX=GPIO1
//   Serial2: TX=GPIO4 (Serial2 not defined by default on all cores - using SerialPIO fallback if needed)
//
// If your wiring uses different GPIOs, change these before .begin()

#define GD32_BAUD  115200
#define WLED_BAUD  115200

// WLED preset IDs - must match presets.json on the WLED board
enum PresetID {
  PASTEL_PINK_SOLID   = 1,
  COTTON_CANDY_BLINK  = 2,
  LAVENDER_BREATHE    = 3,
  MINT_WIPE           = 4,
  PEACH_SCANNER       = 5,
  BUBBLEGUM_DUAL_SCAN = 6,
  SKY_THEATER         = 7,
  PURPLE_RUNNER       = 8,
  SHOOTING_STARS      = 9,
  KITTEN_EYES_SPARKLE = 10,
  YARN_CHASE          = 11,
  PASTEL_RAINBOW      = 12,
  FIREWORKS_ON_BLACK  = 13,
  BLACK_COMET_CHASE   = 14
};

String gd32Buffer = "";
String wledBuffer = "";

// ---- Send a preset select command to WLED over its serial JSON API ----
void wledSelectPreset(int presetId) {
  StaticJsonDocument<64> doc;
  doc["ps"] = presetId;   // "ps" = preset select, per WLED JSON API
  serializeJson(doc, Serial2);
  Serial2.println();
}

// ---- Send raw on/off to WLED ----
void wledSetPower(bool on) {
  StaticJsonDocument<32> doc;
  doc["on"] = on;
  serializeJson(doc, Serial2);
  Serial2.println();
}

// ---- Send brightness (0-255) to WLED ----
void wledSetBrightness(uint8_t bri) {
  StaticJsonDocument<32> doc;
  doc["bri"] = bri;
  serializeJson(doc, Serial2);
  Serial2.println();
}

// ---- Parse a line of text coming from the GD32 over UART0 ----
// Expected simple command format (placeholder protocol until GD32 firmware
// is written - adjust this to match whatever the GD32 side actually sends):
//   "PRESET:3"    -> select preset 3
//   "ON"          -> power on
//   "OFF"         -> power off
//   "BRI:180"     -> set brightness to 180
void handleGD32Command(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd.startsWith("PRESET:")) {
    int id = cmd.substring(7).toInt();
    wledSelectPreset(id);
  } else if (cmd == "ON") {
    wledSetPower(true);
  } else if (cmd == "OFF") {
    wledSetPower(false);
  } else if (cmd.startsWith("BRI:")) {
    int val = cmd.substring(4).toInt();
    wledSetBrightness((uint8_t)constrain(val, 0, 255));
  }
  // Unknown commands are ignored for now - expand as GD32 protocol firms up
}

void setup() {
  // Debug/USB serial for monitoring during development
  Serial.begin(115200);

  // UART0 to GD32 mainboard
  Serial1.setRX(1);
  Serial1.setTX(0);
  Serial1.begin(GD32_BAUD);

  // UART1 to WLED board
  Serial2.setRX(5);
  Serial2.setTX(4);
  Serial2.begin(WLED_BAUD);

  gd32Buffer.reserve(64);
  wledBuffer.reserve(128);
}

void loop() {
  // ---- Read from GD32, act on commands ----
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n') {
      handleGD32Command(gd32Buffer);
      gd32Buffer = "";
    } else if (c != '\r') {
      gd32Buffer += c;
    }
  }

  // ---- Read from WLED (state responses), just log for now ----
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\n') {
      Serial.print("[WLED] ");
      Serial.println(wledBuffer);
      wledBuffer = "";
    } else if (c != '\r') {
      wledBuffer += c;
    }
  }

  // Knob/encoder lives on the GD32 side (wired into its ENA/ENB/BTN pins),
  // not the Pico. GD32 firmware reads it and sends PRESET/BRI/ON/OFF
  // commands over UART0, handled above.
}
