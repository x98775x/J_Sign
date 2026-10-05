#include <Arduino.h>
#include <ArduinoJson.h>

// ---- UART assignments ----
// Serial1 (UART0) -> GD32F205 mainboard, via its UART4 header (RX4/TX4/GND)
// Serial2 (UART1) -> WLED ESP8266 board, via its serial pins
//
// GP0/GP1 are hardwired to the onboard ESP8285 on this Pico W clone.
// GD32 link moved to GP12(TX)/GP13(RX) instead.
// WLED link on GP4(TX)/GP5(RX) - these are free.

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

// ---- Send a preset select command to WLED ----
void wledSelectPreset(int presetId) {
  StaticJsonDocument<64> doc;
  doc["ps"] = presetId;
  serializeJson(doc, Serial2);
  Serial2.println();
}

// ---- Send on/off to WLED ----
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

// ---- Send primary color (RGB) to WLED ----
void wledSetColor(uint8_t r, uint8_t g, uint8_t b) {
  StaticJsonDocument<128> doc;
  JsonArray seg = doc.createNestedArray("seg");
  JsonObject s = seg.createNestedObject();
  JsonArray col = s.createNestedArray("col");
  JsonArray c0 = col.createNestedArray();
  c0.add(r); c0.add(g); c0.add(b);
  serializeJson(doc, Serial2);
  Serial2.println();
}

// ---- Send effect ID to WLED ----
void wledSetEffect(int fx) {
  StaticJsonDocument<64> doc;
  JsonArray seg = doc.createNestedArray("seg");
  JsonObject s = seg.createNestedObject();
  s["fx"] = fx;
  serializeJson(doc, Serial2);
  Serial2.println();
}

// ---- Send speed to WLED ----
void wledSetSpeed(uint8_t sx) {
  StaticJsonDocument<64> doc;
  JsonArray seg = doc.createNestedArray("seg");
  JsonObject s = seg.createNestedObject();
  s["sx"] = sx;
  serializeJson(doc, Serial2);
  Serial2.println();
}

// ---- Send intensity to WLED ----
void wledSetIntensity(uint8_t ix) {
  StaticJsonDocument<64> doc;
  JsonArray seg = doc.createNestedArray("seg");
  JsonObject s = seg.createNestedObject();
  s["ix"] = ix;
  serializeJson(doc, Serial2);
  Serial2.println();
}

// ---- Save current WLED state as preset slot n ----
void wledSavePreset(int slot) {
  // WLED serial doesn't have a direct "save preset" JSON command,
  // but we can send a preset write via the API format.
  // This triggers WLED's preset save to flash at slot n.
  StaticJsonDocument<64> doc;
  doc["psave"] = slot;
  serializeJson(doc, Serial2);
  Serial2.println();
}

// ---- Parse a line of text coming from GD32 over UART0 ----
// Full command set:
//   PRESET:n      -> select preset n
//   ON            -> power on
//   OFF           -> power off
//   BRI:n         -> brightness 0-255
//   COLOR:r,g,b   -> set primary color
//   FX:n          -> set effect by ID
//   SX:n          -> set speed 0-255
//   IX:n          -> set intensity 0-255
//   SAVE:n        -> save current state as preset slot n
void handleGD32Command(String cmd) {
  cmd.trim();
  Serial.print("Received: ");
  Serial.println(cmd);
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
  } else if (cmd.startsWith("COLOR:")) {
    // Parse r,g,b
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
    int fx = cmd.substring(3).toInt();
    wledSetEffect(fx);
  } else if (cmd.startsWith("SX:")) {
    int sx = cmd.substring(3).toInt();
    wledSetSpeed((uint8_t)constrain(sx, 0, 255));
  } else if (cmd.startsWith("IX:")) {
    int ix = cmd.substring(3).toInt();
    wledSetIntensity((uint8_t)constrain(ix, 0, 255));
  } else if (cmd.startsWith("SAVE:")) {
    int slot = cmd.substring(5).toInt();
    wledSavePreset(slot);
  }
  // Unknown commands ignored - expand as needed
}

void setup() {
  Serial.begin(115200);

  // UART0 to GD32 mainboard
  Serial1.setRX(13);
  Serial1.setTX(12);
  Serial1.begin(GD32_BAUD);

  // UART1 to WLED board
  Serial2.setRX(5);
  Serial2.setTX(4);
  Serial2.begin(WLED_BAUD);

  gd32Buffer.reserve(64);
  wledBuffer.reserve(128);
}

void loop() {
  //------ Testing ------
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
        handleGD32Command(gd32Buffer);
        gd32Buffer = "";
    } else if (c != '\r') {
        gd32Buffer += c;
    }
  }

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

  // ---- Read from WLED (state responses), log to USB serial ----
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

  // Knob/encoder lives on GD32 side, not Pico.
  // GD32 reads it and sends commands above.
}
