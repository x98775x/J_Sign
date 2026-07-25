#pragma once
#include <Arduino.h>
#include <lvgl.h>

// ============================================================
// Custom LED Editor Screen
// Lets user pick color, effect, speed, intensity, save preset
// Commands sent over UART to Pico bridge:
//   COLOR:r,g,b   -> set primary color
//   FX:id         -> set effect by ID
//   SX:val        -> set speed (0-255)
//   IX:val        -> set intensity (0-255)
//   SAVE:n        -> save current state as preset slot n
// ============================================================

// Forward declares from main.cpp
void send_cmd(const char *cmd);
void show_menu();

// ---- Effect names (WLED built-in order) ----
static const char *effect_names[] = {
  "Solid", "Blink", "Breathe", "Wipe", "Wipe Random",
  "Random Colors", "Sweep", "Dynamic", "Colorloop", "Rainbow",
  "Scan", "Dual Scan", "Fade", "Theater", "Theater Rainbow",
  "Running", "Saw", "Twinkle", "Dissolve", "Dissolve Rnd",
  "Sparkle", "Sparkle Dark", "Sparkle+", "Strobe", "Strobe Rainbow",
  "Mega Strobe", "Blink Rainbow", "Android", "Chase", "Chase Random",
  "Chase Rainbow", "Chase Flash", "Chase Flash Rnd", "Rainbow Runner",
  "Colorful", "Traffic Light", "Sweep Random", "Running 2", "Red & Blue",
  "Stream", "Scanner", "Lighthouse", "Fireworks", "Rain",
  "Merry Christmas", "Fire Flicker", "Gradient", "Loading", "Police",
  "Police All", "Two Dots", "Two Areas", "Circus", "Halloween",
  "Tri Chase", "Tri Wipe", "Tri Fade", "Lightning", "ICU",
  "Multi Comet", "Scanner Dual", "Stream 2", "Oscillate", "Pride 2015",
  "Juggle", "Palette", "Fire 2012", "Colorwaves", "BPM",
  "Fill Noise", "Noise 1", "Noise 2", "Noise 3", "Noise 4",
  "Colortwinkles", "Lake", "Meteor", "Meteor Smooth", "Railway",
  "Ripple", "Twinklefox", "Twinklecat", "Halloween Eyes", "Solid Pattern",
  "Solid Pattern Tri", "Spots", "Spots Fade", "Glitter", "Candle",
  "Fireworks Starburst", "Fireworks 1D", "Bouncing Balls", "Sinelon",
  "Sinelon Dual", "Sinelon Rainbow", "Popcorn", "Drip", "Plasma",
  "Percent", "Ripple Rainbow", "Heartbeat", "Pacifica", "Candle Multi",
  "Solid Glitter", "Sunrise", "Phased", "Twinkleup", "Noise Pal",
  "Sine", "Phased Noise", "Flow", "Chunchun", "Dancing Shadows",
  "Washing Machine"
};
#define NUM_EFFECTS (sizeof(effect_names)/sizeof(effect_names[0]))

// ---- State ----
static uint8_t custom_r = 255, custom_g = 182, custom_b = 213; // pastel pink default
static int     custom_fx = 0;
static uint8_t custom_sx = 128;
static uint8_t custom_ix = 128;
static int     save_slot = 15; // default save to slot 15+

// ---- LVGL objects ----
static lv_obj_t *lbl_color_preview = NULL;
static lv_obj_t *slider_r = NULL, *slider_g = NULL, *slider_b = NULL;
static lv_obj_t *lbl_r = NULL, *lbl_g = NULL, *lbl_b = NULL;
static lv_obj_t *roller_fx = NULL;
static lv_obj_t *slider_sx = NULL, *slider_ix = NULL;
static lv_obj_t *lbl_sx = NULL, *lbl_ix = NULL;
static lv_obj_t *color_box = NULL;

// ---- Send current color to WLED ----
static void send_color() {
  char buf[32];
  snprintf(buf, sizeof(buf), "COLOR:%d,%d,%d", custom_r, custom_g, custom_b);
  send_cmd(buf);
}

static void send_fx() {
  char buf[16];
  snprintf(buf, sizeof(buf), "FX:%d", custom_fx);
  send_cmd(buf);
}

static void send_sx() {
  char buf[16];
  snprintf(buf, sizeof(buf), "SX:%d", custom_sx);
  send_cmd(buf);
}

static void send_ix() {
  char buf[16];
  snprintf(buf, sizeof(buf), "IX:%d", custom_ix);
  send_cmd(buf);
}

// ---- Update color preview box ----
static void refresh_color_box() {
  if (!color_box) return;
  lv_obj_set_style_bg_color(color_box,
    lv_color_make(custom_r, custom_g, custom_b), 0);
}

// ---- Slider callbacks ----
static void slider_r_cb(lv_event_t *e) {
  custom_r = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
  char buf[8]; snprintf(buf, sizeof(buf), "R: %d", custom_r);
  lv_label_set_text(lbl_r, buf);
  refresh_color_box();
  send_color();
}
static void slider_g_cb(lv_event_t *e) {
  custom_g = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
  char buf[8]; snprintf(buf, sizeof(buf), "G: %d", custom_g);
  lv_label_set_text(lbl_g, buf);
  refresh_color_box();
  send_color();
}
static void slider_b_cb(lv_event_t *e) {
  custom_b = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
  char buf[8]; snprintf(buf, sizeof(buf), "B: %d", custom_b);
  lv_label_set_text(lbl_b, buf);
  refresh_color_box();
  send_color();
}
static void slider_sx_cb(lv_event_t *e) {
  custom_sx = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
  char buf[12]; snprintf(buf, sizeof(buf), "Speed: %d", custom_sx);
  lv_label_set_text(lbl_sx, buf);
  send_sx();
}
static void slider_ix_cb(lv_event_t *e) {
  custom_ix = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
  char buf[16]; snprintf(buf, sizeof(buf), "Intensity: %d", custom_ix);
  lv_label_set_text(lbl_ix, buf);
  send_ix();
}

// ---- Effect roller callback ----
static void roller_fx_cb(lv_event_t *e) {
  custom_fx = lv_roller_get_selected(lv_event_get_target(e));
  send_fx();
}

// ---- Save preset callback ----
static void save_preset_cb(lv_event_t *e) {
  char buf[16];
  snprintf(buf, sizeof(buf), "SAVE:%d", save_slot);
  send_cmd(buf);
  // Flash the button briefly to confirm
  lv_obj_t *btn = lv_event_get_target(e);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xAAFFC3), 0);
  // TODO: add a timer to restore color after 500ms
}

static void custom_back_cb(lv_event_t *e) {
  show_menu();
}

// ---- Build the custom screen ----
// Call once, pass in the screen object already created in main.cpp
void custom_build_ui(lv_obj_t *scr,
                     lv_color_t col_pink,
                     lv_color_t col_lavender,
                     lv_color_t col_mint,
                     lv_color_t col_peach,
                     lv_color_t col_sky,
                     lv_color_t col_dark,
                     lv_color_t col_white) {

  // ---- Back button ----
  lv_obj_t *back = lv_btn_create(scr);
  lv_obj_set_size(back, 100, 40);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 10, 10);
  lv_obj_set_style_bg_color(back, col_lavender, 0);
  lv_obj_set_style_radius(back, 10, 0);
  lv_obj_set_style_border_width(back, 0, 0);
  lv_obj_add_event_cb(back, custom_back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *back_lbl = lv_label_create(back);
  lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
  lv_obj_set_style_text_color(back_lbl, col_dark, 0);
  lv_obj_center(back_lbl);

  // ---- Title ----
  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, LV_SYMBOL_EDIT " Custom");
  lv_obj_set_style_text_color(title, col_pink, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

  // ============================================================
  // LEFT PANEL: Color sliders + preview box
  // ============================================================

  // Color preview box
  color_box = lv_obj_create(scr);
  lv_obj_set_size(color_box, 60, 60);
  lv_obj_align(color_box, LV_ALIGN_TOP_LEFT, 15, 60);
  lv_obj_set_style_bg_color(color_box, lv_color_make(custom_r, custom_g, custom_b), 0);
  lv_obj_set_style_radius(color_box, 8, 0);
  lv_obj_set_style_border_color(color_box, col_white, 0);
  lv_obj_set_style_border_width(color_box, 2, 0);

  // R slider
  char rbuf[8], gbuf[8], bbuf[8];
  snprintf(rbuf, sizeof(rbuf), "R: %d", custom_r);
  snprintf(gbuf, sizeof(gbuf), "G: %d", custom_g);
  snprintf(bbuf, sizeof(bbuf), "B: %d", custom_b);

  lbl_r = lv_label_create(scr);
  lv_label_set_text(lbl_r, rbuf);
  lv_obj_set_style_text_color(lbl_r, lv_color_hex(0xFF6B6B), 0);
  lv_obj_set_style_text_font(lbl_r, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_r, LV_ALIGN_TOP_LEFT, 85, 65);

  slider_r = lv_slider_create(scr);
  lv_slider_set_range(slider_r, 0, 255);
  lv_slider_set_value(slider_r, custom_r, LV_ANIM_OFF);
  lv_obj_set_size(slider_r, 155, 12);
  lv_obj_align(slider_r, LV_ALIGN_TOP_LEFT, 85, 82);
  lv_obj_set_style_bg_color(slider_r, lv_color_hex(0xFF6B6B), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider_r, lv_color_hex(0xFF6B6B), LV_PART_KNOB);
  lv_obj_add_event_cb(slider_r, slider_r_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // G slider
  lbl_g = lv_label_create(scr);
  lv_label_set_text(lbl_g, gbuf);
  lv_obj_set_style_text_color(lbl_g, lv_color_hex(0x6BFF6B), 0);
  lv_obj_set_style_text_font(lbl_g, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_g, LV_ALIGN_TOP_LEFT, 85, 98);

  slider_g = lv_slider_create(scr);
  lv_slider_set_range(slider_g, 0, 255);
  lv_slider_set_value(slider_g, custom_g, LV_ANIM_OFF);
  lv_obj_set_size(slider_g, 155, 12);
  lv_obj_align(slider_g, LV_ALIGN_TOP_LEFT, 85, 115);
  lv_obj_set_style_bg_color(slider_g, lv_color_hex(0x6BFF6B), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider_g, lv_color_hex(0x6BFF6B), LV_PART_KNOB);
  lv_obj_add_event_cb(slider_g, slider_g_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // B slider
  lbl_b = lv_label_create(scr);
  lv_label_set_text(lbl_b, bbuf);
  lv_obj_set_style_text_color(lbl_b, lv_color_hex(0x6B9BFF), 0);
  lv_obj_set_style_text_font(lbl_b, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_b, LV_ALIGN_TOP_LEFT, 85, 131);

  slider_b = lv_slider_create(scr);
  lv_slider_set_range(slider_b, 0, 255);
  lv_slider_set_value(slider_b, custom_b, LV_ANIM_OFF);
  lv_obj_set_size(slider_b, 155, 12);
  lv_obj_align(slider_b, LV_ALIGN_TOP_LEFT, 85, 148);
  lv_obj_set_style_bg_color(slider_b, lv_color_hex(0x6B9BFF), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider_b, lv_color_hex(0x6B9BFF), LV_PART_KNOB);
  lv_obj_add_event_cb(slider_b, slider_b_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // ---- Speed slider ----
  lbl_sx = lv_label_create(scr);
  char sxbuf[16]; snprintf(sxbuf, sizeof(sxbuf), "Speed: %d", custom_sx);
  lv_label_set_text(lbl_sx, sxbuf);
  lv_obj_set_style_text_color(lbl_sx, col_mint, 0);
  lv_obj_set_style_text_font(lbl_sx, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_sx, LV_ALIGN_TOP_LEFT, 15, 175);

  slider_sx = lv_slider_create(scr);
  lv_slider_set_range(slider_sx, 0, 255);
  lv_slider_set_value(slider_sx, custom_sx, LV_ANIM_OFF);
  lv_obj_set_size(slider_sx, 225, 12);
  lv_obj_align(slider_sx, LV_ALIGN_TOP_LEFT, 15, 195);
  lv_obj_set_style_bg_color(slider_sx, col_mint, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider_sx, col_mint, LV_PART_KNOB);
  lv_obj_add_event_cb(slider_sx, slider_sx_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // ---- Intensity slider ----
  lbl_ix = lv_label_create(scr);
  char ixbuf[16]; snprintf(ixbuf, sizeof(ixbuf), "Intensity: %d", custom_ix);
  lv_label_set_text(lbl_ix, ixbuf);
  lv_obj_set_style_text_color(lbl_ix, col_peach, 0);
  lv_obj_set_style_text_font(lbl_ix, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_ix, LV_ALIGN_TOP_LEFT, 15, 215);

  slider_ix = lv_slider_create(scr);
  lv_slider_set_range(slider_ix, 0, 255);
  lv_slider_set_value(slider_ix, custom_ix, LV_ANIM_OFF);
  lv_obj_set_size(slider_ix, 225, 12);
  lv_obj_align(slider_ix, LV_ALIGN_TOP_LEFT, 15, 235);
  lv_obj_set_style_bg_color(slider_ix, col_peach, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider_ix, col_peach, LV_PART_KNOB);
  lv_obj_add_event_cb(slider_ix, slider_ix_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // ---- Save preset button ----
  lv_obj_t *save_btn = lv_btn_create(scr);
  lv_obj_set_size(save_btn, 140, 45);
  lv_obj_align(save_btn, LV_ALIGN_BOTTOM_LEFT, 15, -15);
  lv_obj_set_style_bg_color(save_btn, col_pink, 0);
  lv_obj_set_style_radius(save_btn, 10, 0);
  lv_obj_set_style_border_width(save_btn, 0, 0);
  lv_obj_add_event_cb(save_btn, save_preset_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *save_lbl = lv_label_create(save_btn);
  lv_label_set_text(save_lbl, LV_SYMBOL_SAVE " Save Preset");
  lv_obj_set_style_text_color(save_lbl, col_dark, 0);
  lv_obj_set_style_text_font(save_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(save_lbl);

  // ============================================================
  // RIGHT PANEL: Effect roller
  // ============================================================

  lv_obj_t *fx_lbl = lv_label_create(scr);
  lv_label_set_text(fx_lbl, "Effect");
  lv_obj_set_style_text_color(fx_lbl, col_sky, 0);
  lv_obj_set_style_text_font(fx_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(fx_lbl, LV_ALIGN_TOP_RIGHT, -15, 55);

  // Build roller options string (newline separated)
  static char roller_opts[4096];
  roller_opts[0] = '\0';
  for (size_t i = 0; i < NUM_EFFECTS; i++) {
    strncat(roller_opts, effect_names[i], sizeof(roller_opts) - strlen(roller_opts) - 2);
    if (i < NUM_EFFECTS - 1)
      strncat(roller_opts, "\n", sizeof(roller_opts) - strlen(roller_opts) - 1);
  }

  roller_fx = lv_roller_create(scr);
  lv_roller_set_options(roller_fx, roller_opts, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(roller_fx, 6);
  lv_roller_set_selected(roller_fx, custom_fx, LV_ANIM_OFF);
  lv_obj_set_size(roller_fx, 200, 200);
  lv_obj_align(roller_fx, LV_ALIGN_TOP_RIGHT, -15, 75);
  lv_obj_set_style_bg_color(roller_fx, col_dark, 0);
  lv_obj_set_style_text_color(roller_fx, col_white, 0);
  lv_obj_set_style_text_color(roller_fx, col_sky, LV_PART_SELECTED);
  lv_obj_set_style_bg_color(roller_fx, lv_color_hex(0x1A1A3E), LV_PART_SELECTED);
  lv_obj_set_style_text_font(roller_fx, &lv_font_montserrat_14, 0);
  lv_obj_set_style_border_color(roller_fx, col_sky, 0);
  lv_obj_set_style_border_width(roller_fx, 1, 0);
  lv_obj_set_style_radius(roller_fx, 8, 0);
  lv_obj_add_event_cb(roller_fx, roller_fx_cb, LV_EVENT_VALUE_CHANGED, NULL);
}
