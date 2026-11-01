/* JULIA's Sign - GD32F205 (BTT TFT35 V3.0.x) UI firmware.
 * GD32 SPL + LVGL 8.3. Original screens ported from the Arduino version. */
#include "compat.h"
#include "splash.h"
#include "music_ui.h"
#include "custom_screen.h"

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SCREEN_W * 20];

/* Opaque screen swap. Never scale the root screen: that exposed unpainted
 * display pixels and caused the white/star-shaped transition artifacts. */
static void ui_load_menu_screen(lv_obj_t *screen) {
  if (lv_scr_act() != screen) lv_scr_load(screen);
}

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

// ============================================================
// Screens
// ============================================================
static bool touch_ready = false;   /* true once the touch panel has been calibrated */

static lv_obj_t *scr_menu   = NULL;
static lv_obj_t *scr_leds   = NULL;
static lv_obj_t *scr_viz    = NULL;
static lv_obj_t *scr_music  = NULL;
static lv_obj_t *scr_custom = NULL;

// ============================================================
// State
// ============================================================
static int  menu_sel   = 0;
static int  led_preset = 1;
static int  led_bri    = 160;
static bool led_on     = true;

static uint32_t last_input_ms  = 0;
static bool     in_screensaver = false;
#define IDLE_TIMEOUT_MS 50000

static int  enc_delta   = 0;
static bool btn_pressed = false;

// ============================================================
// Send command to ESP32 over UART4
// ============================================================
void send_cmd(const char *cmd) {
  hal_uart_puts(cmd);
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

void build_screensaver(void) {
  if (scr_saver) { lv_obj_del(scr_saver); scr_saver = NULL; }
  scr_saver = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_saver, COL_BG, 0);
  lv_obj_set_style_bg_opa(scr_saver, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr_saver, LV_OBJ_FLAG_SCROLLABLE);

  lv_color_t cols[] = { COL_PINK, COL_LAVENDER, COL_MINT, COL_PEACH, COL_SKY };
  for (int i = 0; i < 5; i++) {
    lv_obj_t *b = lv_obj_create(scr_saver);
    lv_obj_set_size(b, 28, 20);
    lv_obj_set_style_bg_color(b, cols[i%5], 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_y(b, 20 + i*55);
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, b);
    lv_anim_set_values(&a, -40, SCREEN_W+40);
    lv_anim_set_time(&a, 6000+i*800);
    lv_anim_set_delay(&a, i*700);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
  }
  for (int i = 0; i < 4; i++) {
    lv_obj_t *s = lv_obj_create(scr_saver);
    lv_obj_set_size(s, 6, 6);
    lv_obj_set_style_bg_color(s, COL_WHITE, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s, 0, 0);
    lv_obj_set_y(s, 10+i*70);
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, s);
    lv_anim_set_values(&a, -20, SCREEN_W+20);
    lv_anim_set_time(&a, 1800+i*300);
    lv_anim_set_delay(&a, i*1200);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
  }
}

// ============================================================
// Helpers
// ============================================================
static lv_obj_t* make_label(lv_obj_t *p, const char *txt,
                              lv_color_t col, const lv_font_t *font) {
  lv_obj_t *l = lv_label_create(p);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_color(l, col, 0);
  lv_obj_set_style_text_font(l, font, 0);
  return l;
}

static lv_obj_t* make_btn_ud(lv_obj_t *p, const char *txt,
                            lv_color_t bg, lv_event_cb_t cb,
                            void *user_data) {
  lv_obj_t *btn = lv_btn_create(p);
  lv_obj_set_style_bg_color(btn, bg, 0);
  lv_obj_set_style_bg_color(btn, lv_color_mix(bg, lv_color_black(), 200),
                              LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn, 12, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_border_color(btn, COL_WHITE, 0);
  if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, txt);
  lv_obj_set_style_text_color(lbl, COL_DARK, 0);
  lv_obj_center(lbl);
  return btn;
}

#define make_btn(p, txt, bg, cb) make_btn_ud((p), (txt), (bg), (cb), NULL)

/* Darken the entire selected button instead of drawing a yellow outline. */
static void set_box_focus(lv_obj_t **boxes, int count, int sel) {
  for (int i = 0; i < count; ++i) {
    if (!boxes[i]) continue;
    lv_obj_set_style_bg_opa(boxes[i], LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(boxes[i],
      i == sel ? lv_color_hex(0x465273) : lv_color_hex(0xCBD4E7), 0);
    lv_obj_set_style_text_color(boxes[i], lv_color_white(), 0);
    lv_obj_set_style_border_width(boxes[i], i == sel ? 2 : 0, 0);
    lv_obj_set_style_border_color(boxes[i], lv_color_hex(0x25334F), 0);
    lv_obj_set_style_outline_width(boxes[i], 0, 0);
  }
}

// ============================================================
// Forward declares
// ============================================================
void show_menu(void);
void show_leds(void);
void show_visualizers(void);
void show_music(void);
void show_custom(void);

// ============================================================
// MAIN MENU - 4 items now
// ============================================================
static lv_obj_t   *menu_btns[4];
static const char *menu_labels[] = {
  LV_SYMBOL_EDIT  " LEDs",
  LV_SYMBOL_IMAGE " Visualizers",
  LV_SYMBOL_AUDIO " Music",
  LV_SYMBOL_SETTINGS " Custom"
};
static lv_color_t menu_colors[4];

static void menu_btn_cb(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  last_input_ms = millis();
  switch(idx) {
    case 0: show_leds();        break;
    case 1: show_visualizers(); break;
    case 2: show_music();       break;
    case 3: show_custom();      break;
  }
}

void refresh_menu_highlight(void) {
  for (int i = 0; i < 4; ++i) {
    if (!menu_btns[i]) continue;
    lv_color_t base = menu_colors[i];
    lv_obj_set_style_bg_color(menu_btns[i],
      i == menu_sel ? lv_color_mix(lv_color_black(), base, 115) : base, 0);
    lv_obj_set_style_border_width(menu_btns[i], i == menu_sel ? 2 : 0, 0);
    lv_obj_set_style_border_color(menu_btns[i], lv_color_hex(0x25334F), 0);
    lv_obj_set_style_outline_width(menu_btns[i], 0, 0);
    lv_obj_t *label = lv_obj_get_child(menu_btns[i], 0);
    if (label) lv_obj_set_style_text_color(label,
      i == menu_sel ? lv_color_white() : COL_DARK, 0);
  }
}

/* Animated rainbow ripples behind the main-menu buttons.  Native LVGL
 * primitives only; no image assets or extra flash resources needed. */
static void rainbow_ripple_cb(void *obj, int32_t diameter) {
  if (lv_scr_act() != scr_menu) return;  /* don't redraw hidden menus */
  lv_obj_t *ring = (lv_obj_t *)obj;
  lv_obj_set_size(ring, (lv_coord_t)diameter, (lv_coord_t)diameter);
  lv_obj_align(ring, LV_ALIGN_CENTER, 0, 0);
}

static void build_rainbow_background(lv_obj_t *parent) {
  const uint32_t colors[] = { 0xFF628E, 0xFFC65C, 0x8CEB89, 0x5ECDF5, 0xAF83FF };
  for (int i = 0; i < 5; ++i) {
    lv_obj_t *ring = lv_obj_create(parent);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(colors[i]), 0);
    lv_obj_set_style_border_opa(ring, LV_OPA_50, 0);
    lv_obj_set_style_border_width(ring, 9, 0);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(ring, 0, 0);
    lv_obj_set_style_shadow_width(ring, 0, 0);
    lv_obj_set_size(ring, 40 + i * 105, 40 + i * 105);
    lv_obj_center(ring);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ring);
    lv_anim_set_exec_cb(&a, rainbow_ripple_cb);
    lv_anim_set_values(&a, 40, 650);
    lv_anim_set_time(&a, 10000);
    lv_anim_set_delay(&a, i * 1700);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
  }
}

void show_menu(void) {
  if (!scr_menu) {
    menu_colors[0] = COL_PINK; menu_colors[1] = COL_LAVENDER;
    menu_colors[2] = COL_MINT; menu_colors[3] = COL_SKY;
    scr_menu = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_menu, COL_DARK, 0);
    lv_obj_set_style_bg_opa(scr_menu, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr_menu, LV_OBJ_FLAG_SCROLLABLE);
    build_rainbow_background(scr_menu);

    lv_obj_t *title = make_label(scr_menu, "JULIA", COL_PINK, &lv_font_montserrat_24);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    // Centered, compact two-column layout.
    for (int i = 0; i < 4; i++) {
      menu_btns[i] = make_btn_ud(scr_menu, menu_labels[i], menu_colors[i],
                             menu_btn_cb, (void*)(intptr_t)i);
      lv_obj_set_size(menu_btns[i], 172, 72);
      lv_obj_align(menu_btns[i], LV_ALIGN_CENTER,
                   (i % 2 == 0) ? -93 : 93,
                   (i < 2) ? -45 : 45);
    }
    refresh_menu_highlight();
  }
  ui_load_menu_screen(scr_menu);
  in_screensaver = false;
  last_input_ms = millis();
}

// ============================================================
// LED SCREEN
// ============================================================
static const char *preset_names[] = {
  "",
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
static lv_obj_t *led_boxes[6];
static int led_focus = 0;
#define LED_BOX_COUNT 6

static lv_obj_t *viz_boxes[2];
static int viz_focus = 0;

void refresh_led_screen(void) {
  if (!lbl_preset_name) return;
  lv_label_set_text(lbl_preset_name, preset_names[led_preset]);
  char buf[24];
  snprintf(buf, sizeof(buf), "Preset %d / %d", led_preset, NUM_PRESETS);
  lv_label_set_text(lbl_preset_num, buf);
  snprintf(buf, sizeof(buf), "Brightness: %d", led_bri);
  lv_label_set_text(lbl_bri, buf);
  lv_bar_set_value(bar_bri, led_bri, LV_ANIM_OFF);
  lv_label_set_text(lbl_power, led_on ? LV_SYMBOL_OK " ON" : LV_SYMBOL_CLOSE " OFF");
}

static void led_back_cb(lv_event_t *e)  { last_input_ms=millis(); show_menu(); }
static void led_power_cb(lv_event_t *e) {
  last_input_ms=millis();
  led_on = !led_on;
  send_cmd(led_on ? "ON" : "OFF");
  refresh_led_screen();
}
static void led_prev_cb(lv_event_t *e) {
  last_input_ms=millis();
  if (led_preset>1) led_preset--;
  send_preset(led_preset); refresh_led_screen();
}
static void led_next_cb(lv_event_t *e) {
  last_input_ms=millis();
  if (led_preset<NUM_PRESETS) led_preset++;
  send_preset(led_preset); refresh_led_screen();
}
static void led_bri_up_cb(lv_event_t *e) {
  last_input_ms=millis();
  led_bri=min(255,led_bri+16); send_bri(led_bri); refresh_led_screen();
}
static void led_bri_dn_cb(lv_event_t *e) {
  last_input_ms=millis();
  led_bri=max(0,led_bri-16); send_bri(led_bri); refresh_led_screen();
}

void show_leds(void) {
  if (!scr_leds) {
    scr_leds = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_leds, COL_DARK, 0);
    lv_obj_set_style_bg_opa(scr_leds, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr_leds, LV_OBJ_FLAG_SCROLLABLE);

    led_boxes[0] = make_btn(scr_leds, LV_SYMBOL_LEFT " Back", COL_LAVENDER, led_back_cb);
    lv_obj_set_size(led_boxes[0], 100, 40); lv_obj_align(led_boxes[0], LV_ALIGN_TOP_LEFT, 10, 10);

    make_label(scr_leds, LV_SYMBOL_EDIT " LEDs", COL_PINK, &lv_font_montserrat_20);
    lv_obj_align(lv_obj_get_child(scr_leds, 1), LV_ALIGN_TOP_MID, 0, 15);

    lbl_preset_name = make_label(scr_leds, "", COL_WHITE, &lv_font_montserrat_16);
    lv_obj_align(lbl_preset_name, LV_ALIGN_CENTER, 0, -60);

    lbl_preset_num = make_label(scr_leds, "", COL_SKY, &lv_font_montserrat_14);
    lv_obj_align(lbl_preset_num, LV_ALIGN_CENTER, 0, -35);

    led_boxes[1] = make_btn(scr_leds, LV_SYMBOL_PREV, COL_PEACH, led_prev_cb);
    lv_obj_set_size(led_boxes[1], 60, 50); lv_obj_align(led_boxes[1], LV_ALIGN_CENTER, -80, -45);

    led_boxes[2] = make_btn(scr_leds, LV_SYMBOL_NEXT, COL_PEACH, led_next_cb);
    lv_obj_set_size(led_boxes[2], 60, 50); lv_obj_align(led_boxes[2], LV_ALIGN_CENTER, 80, -45);

    lbl_bri = make_label(scr_leds, "", COL_MINT, &lv_font_montserrat_14);
    lv_obj_align(lbl_bri, LV_ALIGN_CENTER, 0, 10);

    bar_bri = lv_bar_create(scr_leds);
    lv_bar_set_range(bar_bri, 0, 255);
    lv_obj_set_size(bar_bri, 280, 20);
    lv_obj_align(bar_bri, LV_ALIGN_CENTER, 0, 35);
    lv_obj_set_style_bg_color(bar_bri, COL_MINT, LV_PART_INDICATOR);

    led_boxes[3] = make_btn(scr_leds, "-", COL_SKY, led_bri_dn_cb);
    lv_obj_set_size(led_boxes[3], 50, 40); lv_obj_align(led_boxes[3], LV_ALIGN_CENTER, -155, 35);

    led_boxes[4] = make_btn(scr_leds, "+", COL_SKY, led_bri_up_cb);
    lv_obj_set_size(led_boxes[4], 50, 40); lv_obj_align(led_boxes[4], LV_ALIGN_CENTER, 155, 35);

    led_boxes[5] = make_btn(scr_leds, "", COL_PINK, led_power_cb);
    lv_obj_set_size(led_boxes[5], 120, 50); lv_obj_align(led_boxes[5], LV_ALIGN_BOTTOM_MID, 0, -15);
    lbl_power = make_label(led_boxes[5], "", COL_DARK, &lv_font_montserrat_16);
    lv_obj_center(lbl_power);

    refresh_led_screen();
    led_focus = 0;
    set_box_focus(led_boxes, LED_BOX_COUNT, led_focus);
  }
  ui_load_menu_screen(scr_leds);
  refresh_led_screen();
  led_focus = 0;
  set_box_focus(led_boxes, LED_BOX_COUNT, led_focus);
  last_input_ms = millis();
}

// ============================================================
// VISUALIZERS SCREEN
// ============================================================
static void viz_back_cb(lv_event_t *e)    { last_input_ms=millis(); show_menu(); }
static void viz_trigger_cb(lv_event_t *e) { last_input_ms = millis() - IDLE_TIMEOUT_MS - 1; }

void show_visualizers(void) {
  if (!scr_viz) {
    scr_viz = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_viz, COL_DARK, 0);
    lv_obj_set_style_bg_opa(scr_viz, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr_viz, LV_OBJ_FLAG_SCROLLABLE);

    viz_boxes[0] = make_btn(scr_viz, LV_SYMBOL_LEFT " Back", COL_LAVENDER, viz_back_cb);
    lv_obj_set_size(viz_boxes[0], 100, 40); lv_obj_align(viz_boxes[0], LV_ALIGN_TOP_LEFT, 10, 10);

    make_label(scr_viz, LV_SYMBOL_IMAGE " Visualizers", COL_LAVENDER, &lv_font_montserrat_20);
    lv_obj_align(lv_obj_get_child(scr_viz,1), LV_ALIGN_TOP_MID, 0, 15);

    lv_obj_t *desc = make_label(scr_viz,
      "Butterflies & shooting stars\ndrift across the screen\nwhen idle for ~50 seconds.",
      COL_WHITE, &lv_font_montserrat_16);
    lv_obj_align(desc, LV_ALIGN_CENTER, 0, -20);
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(desc, 360);
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);

    viz_boxes[1] = make_btn(scr_viz, LV_SYMBOL_PLAY " Preview Now", COL_MINT, viz_trigger_cb);
    lv_obj_set_size(viz_boxes[1], 180, 50); lv_obj_align(viz_boxes[1], LV_ALIGN_BOTTOM_MID, 0, -15);
  }
  ui_load_menu_screen(scr_viz);
  viz_focus = 0;
  set_box_focus(viz_boxes, 2, viz_focus);
  last_input_ms = millis();
}

// ============================================================
// MUSIC SCREEN
// ============================================================
static void music_back_cb(lv_event_t *e) { last_input_ms=millis(); show_menu(); }

void show_music(void) {
  if (!scr_music) {
    scr_music = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_music, COL_DARK, 0);
    lv_obj_set_style_bg_opa(scr_music, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr_music, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = make_btn(scr_music, LV_SYMBOL_LEFT " Back", COL_LAVENDER, music_back_cb);
    lv_obj_set_size(back, 100, 40); lv_obj_align(back, LV_ALIGN_TOP_LEFT, 10, 10);

    make_label(scr_music, LV_SYMBOL_AUDIO " Music", COL_PEACH, &lv_font_montserrat_20);
    lv_obj_align(lv_obj_get_child(scr_music,1), LV_ALIGN_TOP_MID, 0, 15);

    music_build_ui(scr_music, music_back_cb,
                   COL_PEACH, COL_LAVENDER, COL_DARK, COL_WHITE, COL_MINT);
    music_set_back_btn(back);
  }
  ui_load_menu_screen(scr_music);
  music_set_back_btn(music_btn_back);
  last_input_ms = millis();
}

// ============================================================
// CUSTOM SCREEN
// ============================================================
static void custom_back_cb_impl(lv_event_t *e) { last_input_ms=millis(); show_menu(); }

void show_custom(void) {
  if (!scr_custom) {
    scr_custom = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_custom, COL_DARK, 0);
    lv_obj_set_style_bg_opa(scr_custom, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr_custom, LV_OBJ_FLAG_SCROLLABLE);

    custom_build_ui(scr_custom,
                    COL_PINK, COL_LAVENDER, COL_MINT,
                    COL_PEACH, COL_SKY, COL_DARK, COL_WHITE);
  }
  ui_load_menu_screen(scr_custom);
  custom_reset_focus();
  last_input_ms = millis();
}

// ============================================================
// Encoder read
// ============================================================
void read_encoder(void) {
  /* One hardware detent generates one UI move. Never replay stale transitions. */
  int turn = hal_enc_delta();
  static uint32_t last_turn_ms = 0;
  if (turn && (uint32_t)(millis() - last_turn_ms) >= 35u) {
    enc_delta = turn > 0 ? 1 : -1;
    last_turn_ms = millis();
    last_input_ms = millis();
  }
  if (hal_enc_click()) {
    btn_pressed = true;
    last_input_ms = millis();
  }
}

// ============================================================
// Handle encoder against current screen
// ============================================================
void handle_input(void) {
  lv_obj_t *cur = lv_scr_act();

  if (enc_delta != 0) {
    if (cur == scr_menu || cur == NULL) {
      menu_sel = constrain(menu_sel + enc_delta, 0, 3);
      refresh_menu_highlight();
    } else if (cur == scr_leds) {
      led_focus = (led_focus + enc_delta + LED_BOX_COUNT) % LED_BOX_COUNT;
      set_box_focus(led_boxes, LED_BOX_COUNT, led_focus);
    } else if (cur == scr_viz) {
      viz_focus = (viz_focus + enc_delta + 2) % 2;
      set_box_focus(viz_boxes, 2, viz_focus);
    } else if (cur == scr_music) {
      music_encoder_rotate(enc_delta);
    } else if (cur == scr_custom) {
      custom_encoder_rotate(enc_delta);
    }
    enc_delta = 0;
  }

  if (btn_pressed) {
    btn_pressed = false;
    if (cur == scr_saver) {
      show_menu();
    } else if (cur == scr_menu || cur == NULL) {
      switch(menu_sel) {
        case 0: show_leds();        break;
        case 1: show_visualizers(); break;
        case 2: show_music();       break;
        case 3: show_custom();      break;
      }
    } else if (cur == scr_leds) {
      switch (led_focus) {
        case 0: led_back_cb(NULL); break;
        case 1: led_prev_cb(NULL); break;
        case 2: led_next_cb(NULL); break;
        case 3: led_bri_dn_cb(NULL); break;
        case 4: led_bri_up_cb(NULL); break;
        case 5: led_power_cb(NULL); break;
      }
    } else if (cur == scr_viz) {
      if (viz_focus == 0) viz_back_cb(NULL);
      else viz_trigger_cb(NULL);
    } else if (cur == scr_music) {
      music_encoder_click();
    } else if (cur == scr_custom) {
      custom_encoder_click();
    }
  }
}

// ============================================================
// Idle screensaver check
// ============================================================
void check_idle(void) {
  if (!in_screensaver && (millis() - last_input_ms > IDLE_TIMEOUT_MS)) {
    in_screensaver = true;
    build_screensaver();
    lv_scr_load(scr_saver);
  }
  if (in_screensaver && (enc_delta != 0 || btn_pressed || (touch_ready && hal_touch_pen()))) {
    in_screensaver = false;
    enc_delta = 0;
    btn_pressed = false;
    show_menu();
  }
}


// ============================================================
// Touch: XPT2046 + 3-point calibration (same affine maths as BTT's firmware)
// Calibration is saved to flash. Hold the knob while powering on to redo it;
// click the knob during calibration to skip touch and use the encoder only.
// ============================================================
static int64_t ts_A, ts_B, ts_C, ts_D, ts_E, ts_F, ts_K;
static bool    cal_from_flash = false;      /* a saved calibration was found at boot */

static void touch_map(uint16_t rx, uint16_t ry, int *x, int *y) {
  int64_t tx = rx, ty = ry;
  int px = (int)((ts_A * tx + ts_B * ty + ts_C) / ts_K);
  int py = (int)((ts_D * tx + ts_E * ty + ts_F) / ts_K);
  *x = constrain(px, 0, SCREEN_W - 1);
  *y = constrain(py, 0, SCREEN_H - 1);
}

static bool touch_calibrate_from(const uint16_t *tx, const uint16_t *ty,
                                 const int *lx, const int *ly) {
  int64_t X1 = tx[0], X2 = tx[1], X3 = tx[2];
  int64_t Y1 = ty[0], Y2 = ty[1], Y3 = ty[2];
  int64_t XL1 = lx[0], XL2 = lx[1], XL3 = lx[2];
  int64_t YL1 = ly[0], YL2 = ly[1], YL3 = ly[2];
  int64_t K = (X1 - X3) * (Y2 - Y3) - (X2 - X3) * (Y1 - Y3);
  if (K > -100000 && K < 100000) return false;          /* taps too close together */
  ts_K = K;
  ts_A = (XL1 - XL3) * (Y2 - Y3) - (XL2 - XL3) * (Y1 - Y3);
  ts_B = (X1 - X3) * (XL2 - XL3) - (XL1 - XL3) * (X2 - X3);
  ts_C = Y1 * (X3 * XL2 - X2 * XL3) + Y2 * (X1 * XL3 - X3 * XL1) + Y3 * (X2 * XL1 - X1 * XL2);
  ts_D = (YL1 - YL3) * (Y2 - Y3) - (YL2 - YL3) * (Y1 - Y3);
  ts_E = (X1 - X3) * (YL2 - YL3) - (YL1 - YL3) * (X2 - X3);
  ts_F = Y1 * (X3 * YL2 - X2 * YL3) + Y2 * (X1 * YL3 - X3 * YL1) + Y3 * (X2 * YL1 - X1 * YL2);
  return true;
}

/* LVGL pointer driver: reports a press only after the pen line has been low for 2 reads in a row. */
static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  (void)drv;
  static lv_coord_t lx = 0, ly = 0;
  static bool pen_prev = false;
  bool pen = touch_ready && hal_touch_pen();
  if (pen && pen_prev) {
    uint16_t rx, ry;
    int x, y;
    if (hal_touch_read(&rx, &ry)) {
      touch_map(rx, ry, &x, &y);
      lx = (lv_coord_t)x;
      ly = (lv_coord_t)y;
    }
    last_input_ms = millis();
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
  pen_prev = pen;
  data->point.x = lx;
  data->point.y = ly;
}

static const int cal_lx[3] = { 40, SCREEN_W - 40, SCREEN_W - 40 };
static const int cal_ly[3] = { 40, 40, SCREEN_H - 40 };

/* Restore the saved calibration (if any). */
static void touch_load_saved(void) {
  uint16_t tx[3], ty[3];
  if (hal_cal_load(tx, ty) && touch_calibrate_from(tx, ty, cal_lx, cal_ly)) {
    touch_ready = true;
    cal_from_flash = true;
  }
}

// ---- calibration screen ----
static lv_obj_t *scr_cal = NULL;
static lv_obj_t *cal_dot = NULL;
static lv_obj_t *cal_lbl = NULL;
static int       cal_step = -1;             /* -1 = not calibrating, 0..2 = waiting for target */
static uint16_t  cal_tx[3], cal_ty[3];
static uint32_t  cal_pen_ms = 0;
static bool      cal_wait_release = false;
static uint32_t  cal_release_since = 0;
static uint32_t  cal_step_accepted_ms = 0;

static void cal_show_step(int i) {
  char buf[64];
  lv_obj_set_pos(cal_dot, cal_lx[i] - 12, cal_ly[i] - 12);
  snprintf(buf, sizeof(buf), "Tap the pink dot  (%d/3)\nPress the knob to skip touch", i + 1);
  lv_label_set_text(cal_lbl, buf);
}

void cal_start(void) {
  if (!scr_cal) {
    scr_cal = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_cal, COL_DARK, 0);
    lv_obj_set_style_bg_opa(scr_cal, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr_cal, LV_OBJ_FLAG_SCROLLABLE);
    cal_lbl = make_label(scr_cal, "", COL_WHITE, &lv_font_montserrat_16);
    lv_obj_set_style_text_align(cal_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(cal_lbl, LV_ALIGN_CENTER, 0, 0);
    cal_dot = lv_obj_create(scr_cal);
    lv_obj_set_size(cal_dot, 24, 24);
    lv_obj_set_style_radius(cal_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cal_dot, COL_PINK, 0);
    lv_obj_set_style_bg_opa(cal_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cal_dot, 3, 0);
    lv_obj_set_style_border_color(cal_dot, COL_WHITE, 0);
    lv_obj_clear_flag(cal_dot, LV_OBJ_FLAG_CLICKABLE);
  }
  touch_ready = false;
  cal_step = 0;
  cal_pen_ms = 0;
  cal_wait_release = false;
  cal_release_since = 0;
  cal_step_accepted_ms = 0;
  cal_show_step(0);
  lv_scr_load(scr_cal);
  last_input_ms = millis();
}

static void cal_tick(void) {
  if (btn_pressed) {                         /* knob click = skip touch, encoder only */
    btn_pressed = false;
    cal_step = -1;
    touch_ready = false;
    show_menu();
    return;
  }
  /* Some XPT2046 panels keep PENIRQ asserted through the next touch.
     Never let the release wait permanently block the second target. */
  if (cal_wait_release) {
    if (!hal_touch_pen()) {
      if (!cal_release_since) cal_release_since = millis();
      if ((uint32_t)(millis() - cal_release_since) >= 35u) {
        cal_wait_release = false;
        cal_release_since = 0;
        cal_pen_ms = 0;
      }
    } else {
      cal_release_since = 0;
    }
    if ((uint32_t)(millis() - cal_step_accepted_ms) < 350u) return;
    /* After 350 ms allow another target even if PENIRQ never went high.
       The coordinate-distance gate below prevents counting a held finger twice. */
    cal_wait_release = false;
    cal_pen_ms = 0;
  }
  if (!hal_touch_pen()) { cal_pen_ms = 0; return; }
  if (cal_pen_ms == 0) { cal_pen_ms = millis(); return; }
  if ((uint32_t)(millis() - cal_pen_ms) < 65u) return;

  uint16_t rx, ry;
  if (!hal_touch_read(&rx, &ry)) return;
  /* Reject the previous point if the IRQ remains low while a finger is held.
     Genuine calibration targets are spaced far apart on the screen. */
  if (cal_step > 0) {
    int dx = (int)rx - (int)cal_tx[cal_step - 1];
    int dy = (int)ry - (int)cal_ty[cal_step - 1];
    if (dx > -140 && dx < 140 && dy > -140 && dy < 140) return;
  }
  cal_tx[cal_step] = rx;
  cal_ty[cal_step] = ry;
  cal_pen_ms = 0;
  cal_wait_release = true;
  cal_release_since = 0;
  cal_step_accepted_ms = millis();
  cal_step++;

  if (cal_step < 3) { cal_show_step(cal_step); return; }

  if (touch_calibrate_from(cal_tx, cal_ty, cal_lx, cal_ly)) {
    hal_cal_save(cal_tx, cal_ty);            /* remembered across power cycles */
    touch_ready = true;
    cal_step = -1;
    show_menu();
  } else {                                   /* degenerate taps - start over */
    cal_step = 0;
    cal_show_step(0);
  }
}

void after_splash(void) {
  /* Saved calibration -> straight to the menu. Hold the knob in during power-up to redo it. */
  if (touch_ready && !hal_enc_button_down()) show_menu();
  else cal_start();
}

// ============================================================
// UART lines coming back from the ESP32
// ============================================================
static void resync_leds(void) {
  send_preset(led_preset);
  send_bri(led_bri);
  send_cmd(led_on ? "ON" : "OFF");
}

extern int sd_music_command(const char *s);
extern void sd_music_init(void);

static void handle_esp_line(const char *l) {
  if(sd_music_command(l))return;
  if (strcmp(l, "READY") == 0 || strcmp(l, "WLED:UP") == 0) resync_leds();   /* push our state again */
  music_handle_line(l);
}

static void poll_uart(void) {
  static char line[96];
  static int  len = 0;
  int c;
  while ((c = hal_uart_getc()) >= 0) {
    if (c == '\n' || c == '\r') {
      if (len > 0) {
        line[len] = '\0';
        handle_esp_line(line);
        len = 0;
      }
    } else if (len < (int)sizeof(line) - 1) {
      line[len++] = (char)c;
    }
  }
}

// ============================================================
// Display flush
// ============================================================
static void disp_flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *c) {
  hal_lcd_flush(a->x1, a->y1, a->x2, a->y2, (const uint16_t *)c);
  lv_disp_flush_ready(d);
}

// ============================================================
// Setup / loop / main
// ============================================================
static void setup(void) {
  hal_init();
  hal_lcd_init();
  sd_music_init();

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SCREEN_W * 20);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res  = SCREEN_W;
  disp_drv.ver_res  = SCREEN_H;
  disp_drv.flush_cb = disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type    = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touch_read_cb;
  lv_indev_drv_register(&indev_drv);

  touch_load_saved();
  last_input_ms = millis();
  resync_leds();
  build_splash_screen();
}

static void loop(void) {
  /* LVGL needs its clock advanced by hand; SysTick drives millis(). */
  static uint32_t last_lv_tick = 0;
  const uint32_t now = millis();
  const uint32_t elapsed = now - last_lv_tick;
  if (elapsed) {
    lv_tick_inc(elapsed);
    last_lv_tick = now;
  }

  poll_uart();
  read_encoder();
  if (cal_step >= 0) {
    enc_delta = 0;
    cal_tick();
  } else {
    check_idle();
    handle_input();
    custom_tick();
  }
  lv_timer_handler();
  delay_ms(5);
}

int main(void) {
  setup();
  for (;;) loop();
}
