#include <Arduino.h>
#include <lvgl.h>
#include "lv_conf.h"

// Include your existing project headers untouched
#include "tft_driver.h"
#include "splash.h"
#include "music_player.h"
#include "custom_screen.h"

// ============================================================================
// ESP32 HARDWARE & TFT BUS DEFINITIONS
// ============================================================================
#define TFT_WR_PIN  5

// Parallel Data Pins (DB0 - DB15) mapping for ESP32
const uint8_t TFT_DB[16] = {
  12, 13, 14, 15, 16, 17, 18, 19,
  21, 22, 23, 25, 26, 27, 32, 33
};

// ESP32 Fast Register Write for DB Pins & WR Line (Optimized to prevent screen lag)
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

// ============================================================================
// UI STYLES & GLOBALS
// ============================================================================
#define COL_PINK lv_color_hex(0xFF007F)
#define NUM_PRESETS 8

static lv_obj_t *scr_menu   = NULL;
static lv_obj_t *scr_leds   = NULL;
static lv_obj_t *scr_saver  = NULL;

static lv_obj_t *menu_btns[4];

static int menu_sel    = 0;
static int led_preset  = 1;
static bool led_on     = true;
static int enc_delta   = 0;
static bool btn_pressed = false;

// Forward Declarations
void show_menu();
void show_leds();
void show_visualizers();
void show_music();
void show_custom();
void refresh_menu_highlight();
void refresh_led_screen();
void send_cmd(const char* cmd);
void send_preset(int preset);

// Helper for UI label creation
lv_obj_t* make_label(lv_obj_t *parent, const char *text, lv_color_t color, const lv_font_t *font) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, color, 0);
  if (font) {
    lv_obj_set_style_text_font(label, font, 0);
  }
  return label;
}

// ============================================================================
// MAIN MENU
// ============================================================================
void refresh_menu_highlight() {
  for (int i = 0; i < 4; i++) {
    if (menu_btns[i] == NULL) continue;
    if (i == menu_sel) {
      lv_obj_set_style_border_width(menu_btns[i], 3, 0);
      lv_obj_set_style_border_color(menu_btns[i], COL_PINK, 0);
    } else {
      lv_obj_set_style_border_width(menu_btns[i], 0, 0);
    }
  }
}

void show_menu() {
  scr_menu = lv_obj_create(NULL);
  
  const char *btn_labels[4] = {"LED Control", "Visualizers", "Music Player", "Custom FX"};
  
  for (int i = 0; i < 4; i++) {
    menu_btns[i] = lv_btn_create(scr_menu);
    lv_obj_set_size(menu_btns[i], 200, 40);
    lv_obj_align(menu_btns[i], LV_ALIGN_TOP_MID, 0, 50 + (i * 50));
    
    lv_obj_t *lbl = lv_label_create(menu_btns[i]);
    lv_label_set_text(lbl, btn_labels[i]);
    lv_obj_center(lbl);
  }

  refresh_menu_highlight();
  lv_scr_load(scr_menu);
}

// ============================================================================
// SUB-SCREENS
// ============================================================================
void show_leds() {
  scr_leds = lv_obj_create(NULL);
  
  lv_obj_t *title = make_label(scr_leds, LV_SYMBOL_EDIT " LEDs", COL_PINK, &lv_font_montserrat_20);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

  refresh_led_screen();
  lv_scr_load(scr_leds);
}

void show_visualizers() {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_t *title = make_label(scr, LV_SYMBOL_AUDIO " Visualizer", COL_PINK, &lv_font_montserrat_20);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
  lv_scr_load(scr);
}

void show_music() {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_t *title = make_label(scr, LV_SYMBOL_PLAY " Music", COL_PINK, &lv_font_montserrat_20);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
  lv_scr_load(scr);
}

void show_custom() {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_t *title = make_label(scr, LV_SYMBOL_SETTINGS " Custom", COL_PINK, &lv_font_montserrat_20);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
  lv_scr_load(scr);
}

void refresh_led_screen() {
  // LED UI state updates here
}

void send_cmd(const char* cmd) {
  // Command dispatch to LED controller
}

void send_preset(int preset) {
  // Preset dispatch logic
}

// ============================================================================
// INPUT HANDLING (Crash-Protected)
// ============================================================================
void handle_input() {
  lv_obj_t *cur = lv_scr_act();
  if (!cur) return;

  if (enc_delta != 0) {
    if (scr_menu && cur == scr_menu) {
      menu_sel = constrain(menu_sel + enc_delta, 0, 3);
      refresh_menu_highlight();
    } else if (scr_leds && cur == scr_leds) {
      led_preset = constrain(led_preset + enc_delta, 1, NUM_PRESETS);
      send_preset(led_preset);
      refresh_led_screen();
    }
    enc_delta = 0;
  }

  if (btn_pressed) {
    btn_pressed = false;
    if (scr_saver && cur == scr_saver) {
      show_menu();
    } else if (scr_menu && cur == scr_menu) {
      switch(menu_sel) {
        case 0: show_leds();        break;
        case 1: show_visualizers(); break;
        case 2: show_music();       break;
        case 3: show_custom();      break;
      }
    } else if (scr_leds && cur == scr_leds) {
      led_on = !led_on;
      send_cmd(led_on ? "ON" : "OFF");
      refresh_led_screen();
    }
  }
}

// ============================================================================
// MAIN ARDUINO SETUP & LOOP
// ============================================================================
void setup() {
  Serial.begin(115200);

  // Set bus pins as output
  pinMode(TFT_WR_PIN, OUTPUT);
  digitalWrite(TFT_WR_PIN, HIGH);
  for (int i = 0; i < 16; i++) {
    pinMode(TFT_DB[i], OUTPUT);
  }

  lv_init();
  show_splash(); // Handled by your splash.h header
}

void loop() {
  lv_timer_handler();
  handle_input();
  
  // Yield to ESP32 Task Watchdog Timer (TWDT)
  delay(5);
}