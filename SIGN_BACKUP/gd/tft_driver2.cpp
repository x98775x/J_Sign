#ifndef TFT_DRIVER_H
#define TFT_DRIVER_H

#include <Arduino.h>

// ESP32 Hardware WR Pin & Parallel DB Pins
#define TFT_WR_PIN  5

static const uint8_t TFT_DB[16] = {
  12, 13, 14, 15, 16, 17, 18, 19,
  21, 22, 23, 25, 26, 27, 32, 33
};

// Direct register write for ESP32 (100x faster than digitalWrite)
inline void tft_write_bus(uint16_t data) {
  for (int i = 0; i < 16; i++) {
    uint8_t pin = TFT_DB[i];
    if (pin < 32) {
      if ((data >> i) & 0x01) {
        GPIO.out_w1ts = (1UL << pin);
      } else {
        GPIO.out_w1tc = (1UL << pin);
      }
    } else {
      if ((data >> i) & 0x01) {
        GPIO.out1_w1ts.val = (1UL << (pin - 32));
      } else {
        GPIO.out1_w1tc.val = (1UL << (pin - 32));
      }
    }
  }

  // Fast Toggle WR Line (GPIO 5)
  GPIO.out_w1tc = (1UL << TFT_WR_PIN); // WR LOW
  GPIO.out_w1ts = (1UL << TFT_WR_PIN); // WR HIGH
}

inline void tft_init_pins() {
  pinMode(TFT_WR_PIN, OUTPUT);
  digitalWrite(TFT_WR_PIN, HIGH);
  for (int i = 0; i < 16; i++) {
    pinMode(TFT_DB[i], OUTPUT);
  }
}

#endif // TFT_DRIVER_H