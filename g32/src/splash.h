#pragma once
#include <Arduino.h>
#include <lvgl.h>

// ============================================================
// Splash screen: "Julia's Sign" with animated sparkles
// Auto-transitions to main menu after SPLASH_DURATION_MS
// ============================================================

#define SPLASH_DURATION_MS 3500
#define NUM_SPARKLES       18

static lv_obj_t *scr_splash = NULL;
static lv_timer_t *splash_timer = NULL;

// Forward declare - defined in main.cpp
void show_menu();

// ---- Sparkle positions relative to screen center ----
// Scattered around the title text
static const int16_t sparkle_x[] = {
  -180, -140, -90, -60, -20, 30, 70, 110, 150, 190,
  -160, -80,   0,  80, 160, -40, 40, 120
};
static const int16_t sparkle_y[] = {
  -20,  30,  -50, 20, -40,  35, -55,  25, -30,  10,
   55,  60,   65,  55,  50,  -65, -60, -45
};

// ---- Sparkle size: mix of tiny dots and small stars ----
static const uint8_t sparkle_size[] = {
  6, 4, 8, 4, 6, 8, 4, 6, 4, 8,
  4, 6, 8, 4, 6, 4, 6, 8
};

// ---- Pastel sparkle colors ----
static const lv_color_t sparkle_colors[] = {
  // repeats to cover NUM_SPARKLES
};

static lv_color_t get_sparkle_color(int i) {
  lv_color_t cols[] = {
    lv_color_hex(0xFFB6D5), // pink
    lv_color_hex(0xC8A2FF), // lavender
    lv_color_hex(0xAAFFC3), // mint
    lv_color_hex(0xFFC896), // peach
    lv_color_hex(0x87CEFA), // sky
    lv_color_hex(0xFFFFFF), // white
  };
  return cols[i % 6];
}

// ---- Opacity animation callback ----
static void sparkle_opa_cb(void *obj, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
}

// ---- Scale animation callback (pulse size) ----
static void sparkle_scale_cb(void *obj, int32_t v) {
  lv_obj_set_style_transform_zoom((lv_obj_t*)obj, v, 0);
}

// ---- Timer: transition to main menu ----
static void splash_done_cb(lv_timer_t *t) {
  lv_timer_del(splash_timer);
  splash_timer = NULL;
  show_menu();
}

// ---- Build a 4-point diamond star shape as an lv_obj ----
static lv_obj_t* make_sparkle_obj(lv_obj_t *parent, lv_color_t col, uint8_t sz) {
  lv_obj_t *s = lv_obj_create(parent);
  lv_obj_set_size(s, sz, sz);
  lv_obj_set_style_bg_color(s, col, 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(s, 0, 0);
  lv_obj_set_style_shadow_width(s, sz * 2, 0);
  lv_obj_set_style_shadow_color(s, col, 0);
  lv_obj_set_style_shadow_opa(s, LV_OPA_50, 0);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  return s;
}

// ---- Main splash builder ----
void build_splash_screen() {
  scr_splash = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_splash, lv_color_hex(0x0D0D1A), 0);
  lv_obj_set_style_bg_opa(scr_splash, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr_splash, LV_OBJ_FLAG_SCROLLABLE);

  // ---- "Julia's" - large pink ----
  lv_obj_t *lbl_julia = lv_label_create(scr_splash);
  lv_label_set_text(lbl_julia, "Julia's");
  lv_obj_set_style_text_font(lbl_julia, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(lbl_julia, lv_color_hex(0xFFB6D5), 0);
  lv_obj_align(lbl_julia, LV_ALIGN_CENTER, 0, -30);

  // ---- Fade-in animation for "Julia's" ----
  {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, lbl_julia);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 1000);
    lv_anim_set_exec_cb(&a, sparkle_opa_cb);
    lv_anim_start(&a);
  }

  // ---- "Sign" - slightly smaller lavender ----
  lv_obj_t *lbl_sign = lv_label_create(scr_splash);
  lv_label_set_text(lbl_sign, "Sign");
  lv_obj_set_style_text_font(lbl_sign, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(lbl_sign, lv_color_hex(0xC8A2FF), 0);
  lv_obj_align(lbl_sign, LV_ALIGN_CENTER, 0, 30);

  // ---- Fade-in for "Sign" with delay ----
  {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, lbl_sign);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 800);
    lv_anim_set_delay(&a, 400);
    lv_anim_set_exec_cb(&a, sparkle_opa_cb);
    lv_anim_start(&a);
  }

  // ---- Decorative pastel line under title ----
  lv_obj_t *line_obj = lv_obj_create(scr_splash);
  lv_obj_set_size(line_obj, 260, 2);
  lv_obj_set_style_bg_color(line_obj, lv_color_hex(0xFFB6D5), 0);
  lv_obj_set_style_bg_opa(line_obj, LV_OPA_60, 0);
  lv_obj_set_style_border_width(line_obj, 0, 0);
  lv_obj_align(line_obj, LV_ALIGN_CENTER, 0, 55);

  // ---- Sparkles scattered around the title ----
  for (int i = 0; i < NUM_SPARKLES; i++) {
    lv_obj_t *sp = make_sparkle_obj(scr_splash,
                                     get_sparkle_color(i),
                                     sparkle_size[i % (sizeof(sparkle_size)/sizeof(sparkle_size[0]))]);

    // Position relative to center
    lv_obj_align(sp, LV_ALIGN_CENTER,
                  sparkle_x[i % (sizeof(sparkle_x)/sizeof(sparkle_x[0]))],
                  sparkle_y[i % (sizeof(sparkle_y)/sizeof(sparkle_y[0]))]);

    // Start invisible
    lv_obj_set_style_opa(sp, LV_OPA_TRANSP, 0);

    // Pulsing opacity: each sparkle has a different phase/duration
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, sp);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 400 + (i * 97) % 400);     // 400-800ms fade in
    lv_anim_set_delay(&a, (i * 137) % 1200);         // staggered start
    lv_anim_set_exec_cb(&a, sparkle_opa_cb);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_repeat_delay(&a, (i * 83) % 600);
    lv_anim_set_playback_time(&a, 300 + (i * 71) % 300); // fade out
    lv_anim_set_playback_delay(&a, (i * 59) % 400);
    lv_anim_start(&a);
  }

  // ---- Small "Made with love" subtitle at bottom ----
  lv_obj_t *sub = lv_label_create(scr_splash);
  lv_label_set_text(sub, "\xe2\x9d\xa4  Made with love");
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(sub, lv_color_hex(0xFFB6D5), 0);
  lv_obj_set_style_opa(sub, LV_OPA_60, 0);
  lv_obj_align(sub, LV_ALIGN_BOTTOM_MID, 0, -18);

  // ---- Load screen and start timer ----
  lv_scr_load(scr_splash);
  splash_timer = lv_timer_create(splash_done_cb, SPLASH_DURATION_MS, NULL);
}
