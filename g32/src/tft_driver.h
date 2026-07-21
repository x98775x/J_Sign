#pragma once
#include <Arduino.h>

// ============================================================
// TFT35 16-bit parallel (8080-style) driver for ILI9488
// Pin mapping per BTT TFT35 pinout sheet:
//   CS  = PD7    RS  = PE2    WR  = PD5    RD  = PD4
//   K (backlight) = PD12
//   DB0-DB15 = PD14,PD15,PD0,PD1,PE7,PE8,PE9,PE10,PE11,PE12,PE13,PE14,PE15,PD8,PD9,PD10
//
// This is a bit-banged implementation (direct GPIO toggling), not the
// STM32 FSMC peripheral. Slower than real FSMC but every pin mapping here
// is exactly what's on the board - no risk of guessing wrong AF mappings.
// If refresh rate becomes a bottleneck later, this is the place to swap
// in proper FSMC register writes.
// ============================================================

// ---- Control pins ----
#define TFT_CS   PD7
#define TFT_RS   PE2
#define TFT_WR   PD5
#define TFT_RD   PD4
#define TFT_BL   PD12

// ---- Data bus pins, DB0..DB15 in order ----
static const uint8_t TFT_DB[16] = {
    PD14, PD15, PD0, PD1, PE7, PE8, PE9, PE10,
    PE11, PE12, PE13, PE14, PE15, PD8, PD9, PD10
};

inline void tft_bus_init() {
    for (int i = 0; i < 16; i++) {
        pinMode(TFT_DB[i], OUTPUT);
    }
    pinMode(TFT_CS, OUTPUT);
    pinMode(TFT_RS, OUTPUT);
    pinMode(TFT_WR, OUTPUT);
    pinMode(TFT_RD, OUTPUT);
    pinMode(TFT_BL, OUTPUT);

    digitalWrite(TFT_CS, HIGH);
    digitalWrite(TFT_WR, HIGH);
    digitalWrite(TFT_RD, HIGH);
    digitalWrite(TFT_BL, HIGH); // backlight on
}

inline void tft_write_bus(uint16_t data) {
    for (int i = 0; i < 16; i++) {
        digitalWrite(TFT_DB[i], (data >> i) & 0x01);
    }
    digitalWrite(TFT_WR, LOW);
    digitalWrite(TFT_WR, HIGH); // rising edge latches data
}

inline void tft_write_cmd(uint8_t cmd) {
    digitalWrite(TFT_CS, LOW);
    digitalWrite(TFT_RS, LOW); // RS low = command
    tft_write_bus(cmd);
    digitalWrite(TFT_CS, HIGH);
}

inline void tft_write_data(uint16_t data) {
    digitalWrite(TFT_CS, LOW);
    digitalWrite(TFT_RS, HIGH); // RS high = data
    tft_write_bus(data);
    digitalWrite(TFT_CS, HIGH);
}

inline void tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    tft_write_cmd(0x2A); // Column address set
    tft_write_data(x0 >> 8);
    tft_write_data(x0 & 0xFF);
    tft_write_data(x1 >> 8);
    tft_write_data(x1 & 0xFF);

    tft_write_cmd(0x2B); // Page address set
    tft_write_data(y0 >> 8);
    tft_write_data(y0 & 0xFF);
    tft_write_data(y1 >> 8);
    tft_write_data(y1 & 0xFF);

    tft_write_cmd(0x2C); // Memory write
}

// ---- ILI9488 init sequence (standard power-on config) ----
inline void tft_init_panel() {
    tft_bus_init();

    // Hardware reset would normally go through a dedicated RST pin.
    // TFT35 pinout doesn't list one separately for this bus - if your
    // panel needs it, wire it and toggle it here before continuing.
    delay(120);

    tft_write_cmd(0x01); // Software reset
    delay(150);

    tft_write_cmd(0x11); // Sleep out
    delay(120);

    tft_write_cmd(0x3A); // Interface pixel format
    tft_write_data(0x55); // 16-bit/pixel (RGB565)

    tft_write_cmd(0x36); // Memory access control
    tft_write_data(0x48); // adjust orientation as needed

    tft_write_cmd(0x29); // Display ON
    delay(50);
}

// ---- LVGL flush callback - the piece that was a stub ----
inline void tft_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint16_t w = (area->x2 - area->x1 + 1);
    uint16_t h = (area->y2 - area->y1 + 1);

    tft_set_window(area->x1, area->y1, area->x2, area->y2);

    digitalWrite(TFT_CS, LOW);
    digitalWrite(TFT_RS, HIGH);
    for (uint32_t i = 0; i < (uint32_t)w * h; i++) {
        tft_write_bus(color_p[i].full);
    }
    digitalWrite(TFT_CS, HIGH);

    lv_disp_flush_ready(disp);
}
