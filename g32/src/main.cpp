#include <Arduino.h>
#include <lvgl.h>
#include "tft_driver.h"
#include "music_player.h"
#include "splash.h"

static const uint16_t SCREEN_W = 480;
static const uint16_t SCREEN_H = 320;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SCREEN_W * 10];

// ============================================================
// EC11 Rotary Encoder pins (per BTT TFT35 pinout)
// ENC: ENA=PA8  ENB=PC9  BTN=PC8
// ============================================================
#define ENC_A   PA8
#define ENC_B   PC9
#define ENC_BTN PC8

// ============================================================
// UART4 to Pico bridge
// TX4=PA0  RX4=... per pinout (we only need TX here)
// Using Serial4 on stm32duino (UART4)
// ============================================================
#define BRIDGE_BAUD 115200

// ============================================================
// Pastel palette
// ============================================================
#define COL_BG       lv_color_hex(0x000000)
#define COL_PINK     lv_color_hex(0xFFB6D5)
#define COL_LAVENDER lv_color_hex(0xC8A2FF)
#define COL_MINT     lv_color_hex(0xAAFFC3)
#define COL_PEACH    lv_color_hex(0xFFC896)
#define COL_SKY      lv_color_hex(0x87CEFA)
#define COL_WHITE    lv_color_hex(0xFFFFFF)
#define COL_DARK     lv_color_hex(0x1A1A2E)
#define COL_ACCENT   lv_color_hex(0xFFB6D5)

// ============================================================
// State
// ============================================================
static lv_obj_t *scr_menu    = NULL;
static lv_obj_t *scr_leds    = NULL;
static lv_obj_t *scr_viz     = NULL;
static lv_obj_t *scr_music   = NULL;

static int  menu_sel       = 0;   // 0=LEDs 1=Visualizers 2=Music
static int  led_preset     = 1;   // current WLED preset (1-14)
static int  led_bri        = 160; // brightness 0-255
static bool led_on         = true;

static uint32_t last_input_ms = 0;
static bool     in_screensaver = false;
#define IDLE_TIMEOUT_MS 50000  // 50 seconds

// encoder state
static int  enc_a_last = HIGH;
static int  enc_delta  = 0;  // +1 or -1 per detent
static bool btn_pressed = false;
static bool btn_last    = HIGH;

// ============================================================
// Send command to Pico over UART4
// ============================================================
void send_cmd(const char *cmd) {
  Serial4.println(cmd);
}

void send_preset(int id) {
  char buf[16];
  snprintf(buf, sizeof(buf), "PRESET:%d", id);
  send_cmd(buf);
}

void send_bri(int bri) {
  char buf[16];
  snprintf(buf, sizeof(buf), "BRI:%d", bri);
  send_cmd(buf);
}

// ============================================================
// Screensaver (idle animation): black bg, butterflies + stars
// ============================================================
static void anim_x_cb(void *obj, int32_t v) {
  lv_obj_set_x((lv_obj_t*)obj, v);
}

static lv_obj_t *scr_saver = NULL;

void build_screensaver() {
  if (scr_saver) {
    lv_obj_del(scr_saver);
    scr_saver = NULL;
  }
  scr_saver = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_saver, COL_BG, 0);
  lv_obj_set_style_bg_opa(scr_saver, LV_OPA_COVER, 0);

  lv_color_t cols[] = { COL_PINK, COL_LAVENDER, COL_MINT, COL_PEACH, COL_SKY };

  for (int i = 0; i < 5; i++) {
    lv_obj_t *b = lv_obj_create(scr_saver);
    lv_obj_set_size(b, 28, 20);
    lv_obj_set_style_bg_color(b, cols[i % 5], 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_y(b, 20 + i * 55);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, b);
    lv_anim_set_values(&a, -40, SCREEN_W + 40);
    lv_anim_set_time(&a, 6000 + i * 800);
    lv_anim_set_delay(&a, i * 700);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_repeat_delay(&a, 300);
    lv_anim_start(&a);
  }

  for (int i = 0; i < 4; i++) {
    lv_obj_t *s = lv_obj_create(scr_saver);
    lv_obj_set_size(s, 6, 6);
    lv_obj_set_style_bg_color(s, COL_WHITE, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s, 0, 0);
    lv_obj_set_y(s, 10 + i * 70);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s);
    lv_anim_set_values(&a, -20, SCREEN_W + 20);
    lv_anim_set_time(&a, 1800 + i * 300);
    lv_anim_set_delay(&a, i * 1200);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_repeat_delay(&a, 200);
    lv_anim_start(&a);
  }
}

// ============================================================
// Helper: make a styled pastel label
// ============================================================
static lv_obj_t* make_label(lv_obj_t *parent, const char *txt,
                              lv_color_t col, const lv_font_t *font) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_label_set_text(lbl, txt);
  lv_obj_set_style_text_color(lbl, col, 0);
  lv_obj_set_style_text_font(lbl, font, 0);
  return lbl;
}

// ============================================================
// Helper: simple pastel button
// ============================================================
static lv_obj_t* make_btn(lv_obj_t *parent, const char *txt,
                            lv_color_t bg, lv_event_cb_t cb) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_style_bg_color(btn, bg, 0);
  lv_obj_set_style_bg_color(btn, lv_color_darken(bg, 40), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn, 12, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, txt);
  lv_obj_set_style_text_color(lbl, COL_DARK, 0);
  lv_obj_center(lbl);
  return btn;
}

// ============================================================
// Forward declares
// ============================================================
void show_menu();
void show_leds();
void show_visualizers();
void show_music();

// ============================================================
// MAIN MENU SCREEN
// 3 big buttons: LEDs / Visualizers / Music
// Highlighted by encoder rotation, activated by click
// ============================================================
static lv_obj_t *menu_btns[3];
static const char *menu_labels[] = { LV_SYMBOL_EDIT " LEDs",
                                      LV_SYMBOL_IMAGE " Visualizers",
                                      LV_SYMBOL_AUDIO " Music" };
static lv_color_t menu_colors[] = { COL_PINK, COL_LAVENDER, COL_MINT };

static void menu_btn_cb(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  last_input_ms = millis();
  switch (idx) {
    case 0: show_leds();        break;
    case 1: show_visualizers(); break;
    case 2: show_music();       break;
  }
}

void refresh_menu_highlight() {
  for (int i = 0; i < 3; i++) {
    if (i == menu_sel) {
      lv_obj_set_style_border_width(menu_btns[i], 3, 0);
      lv_obj_set_style_border_color(menu_btns[i], COL_WHITE, 0);
    } else {
      lv_obj_set_style_border_width(menu_btns[i], 0, 0);
    }
  }
}

void show_menu() {
  if (!scr_menu) {
    scr_menu = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_menu, COL_DARK, 0);
    lv_obj_set_style_bg_opa(scr_menu, LV_OPA_COVER, 0);

    // Title
    lv_obj_t *title = make_label(scr_menu, "JULIA", COL_PINK, &lv_font_montserrat_24);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    // 3 menu buttons arranged horizontally
    for (int i = 0; i < 3; i++) {
      menu_btns[i] = make_btn(scr_menu, menu_labels[i], menu_colors[i], menu_btn_cb);
      lv_obj_set_size(menu_btns[i], 140, 200);
      lv_obj_align(menu_btns[i], LV_ALIGN_CENTER, (i - 1) * 155, 20);
      lv_obj_set_user_data(menu_btns[i], (void*)(intptr_t)i);
    }
    refresh_menu_highlight();
  }
  lv_scr_load(scr_menu);
  in_screensaver = false;
  last_input_ms = millis();
}

// ============================================================
// LED SCREEN
// Shows current preset name + brightness bar
// Encoder: rotate = cycle presets, click = toggle on/off
// ============================================================
static const char *preset_names[] = {
  "", // index 0 unused
  "Pastel Pink Solid",  "Cotton Candy Blink", "Lavender Breathe",
  "Mint Wipe",          "Peach Scanner",      "Bubblegum Dual",
  "Sky Theater",        "Purple Runner",      "Shooting Stars",
  "Kitten Sparkle",     "Yarn Chase",         "Pastel Rainbow",
  "Fireworks on Black", "Black Comet Chase"
};
#define NUM_PRESETS 14

static lv_obj_t *lbl_preset_name = NULL;
static lv_obj_t *lbl_preset_num  = NULL;
static lv_obj_t *lbl_bri         = NULL;
static lv_obj_t *bar_bri         = NULL;
static lv_obj_t *lbl_power       = NULL;

void refresh_led_screen() {
  if (!lbl_preset_name) return;
  lv_label_set_text(lbl_preset_name, preset_names[led_preset]);
  char buf[16];
  snprintf(buf, sizeof(buf), "Preset %d / %d", led_preset, NUM_PRESETS);
  lv_label_set_text(lbl_preset_num, buf);
  snprintf(buf, sizeof(buf), "Brightness: %d", led_bri);
  lv_label_set_text(lbl_bri, buf);
  lv_bar_set_value(bar_bri, led_bri, LV_ANIM_OFF);
  lv_label_set_text(lbl_power, led_on ? LV_SYMBOL_OK " ON" : LV_SYMBOL_CLOSE " OFF");
}

static void led_back_cb(lv_event_t *e) {
  last_input_ms = millis();
  show_menu();
}
static void led_power_cb(lv_event_t *e) {
  last_input_ms = millis();
  led_on = !led_on;
  send_cmd(led_on ? "ON" : "OFF");
  refresh_led_screen();
}
static void led_prev_cb(lv_event_t *e) {
  last_input_ms = millis();
  if (led_preset > 1) led_preset--;
  send_preset(led_preset);
  refresh_led_screen();
}
static void led_next_cb(lv_event_t *e) {
  last_input_ms = millis();
  if (led_preset < NUM_PRESETS) led_preset++;
  send_preset(led_preset);
  refresh_led_screen();
}
static void led_bri_up_cb(lv_event_t *e) {
  last_input_ms = millis();
  led_bri = min(255, led_bri + 16);
  send_bri(led_bri);
  refresh_led_screen();
}
static void led_bri_dn_cb(lv_event_t *e) {
  last_input_ms = millis();
  led_bri = max(0, led_bri - 16);
  send_bri(led_bri);
  refresh_led_screen();
}

void show_leds() {
  if (!scr_leds) {
    scr_leds = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_leds, COL_DARK, 0);
    lv_obj_set_style_bg_opa(scr_leds, LV_OPA_COVER, 0);

    // Back button
    lv_obj_t *back = make_btn(scr_leds, LV_SYMBOL_LEFT " Back", COL_LAVENDER, led_back_cb);
    lv_obj_set_size(back, 100, 40);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 10, 10);

    // Title
    lv_obj_t *title = make_label(scr_leds, LV_SYMBOL_EDIT " LEDs", COL_PINK, &lv_font_montserrat_20);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    // Preset name
    lbl_preset_name = make_label(scr_leds, "", COL_WHITE, &lv_font_montserrat_16);
    lv_obj_align(lbl_preset_name, LV_ALIGN_CENTER, 0, -60);

    lbl_preset_num = make_label(scr_leds, "", COL_SKY, &lv_font_montserrat_14);
    lv_obj_align(lbl_preset_num, LV_ALIGN_CENTER, 0, -35);

    // Prev / Next
    lv_obj_t *prev = make_btn(scr_leds, LV_SYMBOL_PREV, COL_PEACH, led_prev_cb);
    lv_obj_set_size(prev, 60, 50);
    lv_obj_align(prev, LV_ALIGN_CENTER, -80, -45);

    lv_obj_t *next = make_btn(scr_leds, LV_SYMBOL_NEXT, COL_PEACH, led_next_cb);
    lv_obj_set_size(next, 60, 50);
    lv_obj_align(next, LV_ALIGN_CENTER, 80, -45);

    // Brightness bar
    lbl_bri = make_label(scr_leds, "", COL_MINT, &lv_font_montserrat_14);
    lv_obj_align(lbl_bri, LV_ALIGN_CENTER, 0, 10);

    bar_bri = lv_bar_create(scr_leds);
    lv_bar_set_range(bar_bri, 0, 255);
    lv_obj_set_size(bar_bri, 280, 20);
    lv_obj_align(bar_bri, LV_ALIGN_CENTER, 0, 35);
    lv_obj_set_style_bg_color(bar_bri, COL_MINT, LV_PART_INDICATOR);

    lv_obj_t *bri_dn = make_btn(scr_leds, "-", COL_SKY, led_bri_dn_cb);
    lv_obj_set_size(bri_dn, 50, 40);
    lv_obj_align(bri_dn, LV_ALIGN_CENTER, -155, 35);

    lv_obj_t *bri_up = make_btn(scr_leds, "+", COL_SKY, led_bri_up_cb);
    lv_obj_set_size(bri_up, 50, 40);
    lv_obj_align(bri_up, LV_ALIGN_CENTER, 155, 35);

    // Power toggle
    lbl_power = make_label(scr_leds, "", COL_PINK, &lv_font_montserrat_16);
    lv_obj_t *pwr = make_btn(scr_leds, "", COL_PINK, led_power_cb);
    lv_obj_set_size(pwr, 120, 50);
    lv_obj_align(pwr, LV_ALIGN_BOTTOM_MID, 0, -15);
    lbl_power = make_label(pwr, led_on ? LV_SYMBOL_OK " ON" : LV_SYMBOL_CLOSE " OFF",
                           COL_DARK, &lv_font_montserrat_16);
    lv_obj_center(lbl_power);

    refresh_led_screen();
  }
  lv_scr_load(scr_leds);
  refresh_led_screen();
  last_input_ms = millis();
}

// ============================================================
// VISUALIZERS SCREEN
// Just shows a message + back button.
// The actual idle screensaver IS the visualizer - this screen
// explains that and lets you trigger it immediately.
// ============================================================
static void viz_back_cb(lv_event_t *e) {
  last_input_ms = millis();
  show_menu();
}
static void viz_trigger_cb(lv_event_t *e) {
  last_input_ms = 0; // force screensaver now
}

void show_visualizers() {
  if (!scr_viz) {
    scr_viz = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_viz, COL_DARK, 0);
    lv_obj_set_style_bg_opa(scr_viz, LV_OPA_COVER, 0);

    lv_obj_t *back = make_btn(scr_viz, LV_SYMBOL_LEFT " Back", COL_LAVENDER, viz_back_cb);
    lv_obj_set_size(back, 100, 40);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 10, 10);

    lv_obj_t *title = make_label(scr_viz, LV_SYMBOL_IMAGE " Visualizers", COL_LAVENDER, &lv_font_montserrat_20);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    lv_obj_t *desc = make_label(scr_viz,
      "Butterflies & shooting stars\ndrift across the screen\nwhen idle for ~50 seconds.",
      COL_WHITE, &lv_font_montserrat_16);
    lv_obj_align(desc, LV_ALIGN_CENTER, 0, -20);
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(desc, 360);
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *trig = make_btn(scr_viz, LV_SYMBOL_PLAY " Preview Now", COL_MINT, viz_trigger_cb);
    lv_obj_set_size(trig, 180, 50);
    lv_obj_align(trig, LV_ALIGN_BOTTOM_MID, 0, -15);
  }
  lv_scr_load(scr_viz);
  last_input_ms = millis();
}

// ============================================================
// MUSIC SCREEN
// SD card audio playback - hardware TBD (amp/speaker not wired yet)
// Shows placeholder UI with file listing area
// ============================================================
static void music_back_cb(lv_event_t *e) {
  last_input_ms = millis();
  show_menu();
}

void show_music() {
  if (!scr_music) {
    scr_music = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_music, COL_DARK, 0);
    lv_obj_set_style_bg_opa(scr_music, LV_OPA_COVER, 0);

    lv_obj_t *back = make_btn(scr_music, LV_SYMBOL_LEFT " Back", COL_LAVENDER, music_back_cb);
    lv_obj_set_size(back, 100, 40);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 10, 10);

    lv_obj_t *title = make_label(scr_music, LV_SYMBOL_AUDIO " Music", COL_PEACH, &lv_font_montserrat_20);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    music_build_ui(scr_music, music_back_cb,
                   COL_PEACH, COL_LAVENDER, COL_DARK, COL_WHITE, COL_MINT);
  }
  lv_scr_load(scr_music);
  last_input_ms = millis();
}

// ============================================================
// Encoder read (called every loop)
// ============================================================
void read_encoder() {
  int a = digitalRead(ENC_A);
  int b = digitalRead(ENC_B);
  int btn = digitalRead(ENC_BTN);

  // Detect rotation (simple state machine)
  if (a != enc_a_last && a == LOW) {
    enc_delta = (b == HIGH) ? +1 : -1;
    last_input_ms = millis();
  }
  enc_a_last = a;

  // Detect button press (active low, debounced by state change)
  if (btn == LOW && btn_last == HIGH) {
    btn_pressed = true;
    last_input_ms = millis();
  }
  btn_last = btn;
}

// ============================================================
// Handle encoder input against current screen
// ============================================================
void handle_input() {
  lv_obj_t *cur = lv_scr_act();

  if (enc_delta != 0) {
    if (cur == scr_menu || cur == NULL) {
      menu_sel = constrain(menu_sel + enc_delta, 0, 2);
      refresh_menu_highlight();
    } else if (cur == scr_leds) {
      led_preset = constrain(led_preset + enc_delta, 1, NUM_PRESETS);
      send_preset(led_preset);
      refresh_led_screen();
    }
    enc_delta = 0;
  }

  if (btn_pressed) {
    btn_pressed = false;
    if (cur == scr_menu || cur == NULL) {
      switch (menu_sel) {
        case 0: show_leds();        break;
        case 1: show_visualizers(); break;
        case 2: show_music();       break;
      }
    } else if (cur == scr_saver) {
      // Any button press wakes from screensaver
      show_menu();
    } else if (cur == scr_leds) {
      led_on = !led_on;
      send_cmd(led_on ? "ON" : "OFF");
      refresh_led_screen();
    }
  }
}

// ============================================================
// Idle timeout -> screensaver
// ============================================================
void check_idle() {
  if (!in_screensaver && (millis() - last_input_ms > IDLE_TIMEOUT_MS)) {
    in_screensaver = true;
    build_screensaver();
    lv_scr_load(scr_saver);
  }
  // Wake on any encoder movement handled in handle_input()
  if (in_screensaver && (enc_delta != 0 || btn_pressed)) {
    in_screensaver = false;
    enc_delta = 0;
    btn_pressed = false;
    show_menu();
  }
}

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(115200);

  // UART4 to Pico bridge (TX4=PA0 per BTT pinout, RX4 unused here)
  Serial4.begin(BRIDGE_BAUD);

  // Encoder pins - active low, pull-up
  pinMode(ENC_A,   INPUT_PULLUP);
  pinMode(ENC_B,   INPUT_PULLUP);
  pinMode(ENC_BTN, INPUT_PULLUP);
  enc_a_last = digitalRead(ENC_A);
  btn_last   = digitalRead(ENC_BTN);

  tft_init_panel();

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SCREEN_W * 10);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res  = SCREEN_W;
  disp_drv.ver_res  = SCREEN_H;
  disp_drv.flush_cb = tft_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  // Boot: show splash screen, auto-transitions to main menu after 3.5s
  last_input_ms = millis();
  send_preset(led_preset);
  send_bri(led_bri);
  send_cmd("ON");
  build_splash_screen();
}

// ============================================================
// Loop
// ============================================================
void loop() {
  read_encoder();
  check_idle();
  handle_input();
  lv_timer_handler();
  delay(5);
}
