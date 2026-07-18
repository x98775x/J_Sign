#include <Arduino.h>
#include <lvgl.h>

// ============================================================
// IMPORTANT - READ BEFORE WIRING ANYTHING
// ============================================================
// The TFT35's LCD panel is NOT SPI. Per the BTT pinout diagram it's a
// 16-bit parallel (8080-style) interface:
//   CS=PD7  RS=PE2  WR=PD5  RD=PD4  K(backlight)=PD12
//   DB0-DB15 = PD14,PD15,PD0,PD1,PE7,PE8,PE9,PE10,PE11,PE12,PE13,PE14,PE15,PD8,PD9,PD10
//
// That means the LVGL flush callback below (tft_flush) is a STUB.
// The real display driver needs to either:
//   (a) bit-bang all 16 data lines + control lines manually, or
//   (b) use the STM32 FSMC peripheral in 16-bit mode (much faster,
//       the "correct" way, but needs proper FSMC init code)
// This is the single biggest remaining chunk of firmware work.
// Everything else here (LVGL setup, UI objects, animation) will run
// fine once tft_flush actually pushes pixels to the panel.
// ============================================================

static const uint16_t SCREEN_W = 480;
static const uint16_t SCREEN_H = 320;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SCREEN_W * 10];

// ---- pastel palette ----
#define COL_BG        lv_color_black()
#define COL_PINK      lv_color_hex(0xFFB6D5)
#define COL_LAVENDER  lv_color_hex(0xC8A2FF)
#define COL_MINT      lv_color_hex(0xAAFFC3)
#define COL_PEACH     lv_color_hex(0xFFC896)
#define COL_SKY       lv_color_hex(0x87CEFA)

// ---- STUB: pushes pixels to the panel. Not implemented yet. ----
void tft_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  // TODO: replace with real FSMC/parallel write once display driver exists
  lv_disp_flush_ready(disp);
}

// ---- Build one simple "butterfly": two triangles (wings) as a styled object ----
static lv_obj_t* make_butterfly(lv_obj_t *parent, lv_color_t color) {
  lv_obj_t *body = lv_obj_create(parent);
  lv_obj_set_size(body, 28, 20);
  lv_obj_set_style_bg_color(body, color, 0);
  lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(body, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  return body;
}

// ---- Build a simple 4-point "star" for the shooting-star effect ----
static lv_obj_t* make_star(lv_obj_t *parent, lv_color_t color) {
  lv_obj_t *star = lv_obj_create(parent);
  lv_obj_set_size(star, 6, 6);
  lv_obj_set_style_bg_color(star, color, 0);
  lv_obj_set_style_bg_opa(star, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(star, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(star, 0, 0);
  return star;
}

// ---- Animation callback: moves an object's x position ----
static void anim_x_cb(void *obj, int32_t v) {
  lv_obj_set_x((lv_obj_t*)obj, v);
}
static void anim_y_cb(void *obj, int32_t v) {
  lv_obj_set_y((lv_obj_t*)obj, v);
}

// ---- Sends an object drifting/flying across the screen, looping ----
static void animate_flyer(lv_obj_t *obj, int32_t startX, int32_t endX,
                           int32_t y, uint32_t durationMs, uint32_t delayMs) {
  lv_obj_set_y(obj, y);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_values(&a, startX, endX);
  lv_anim_set_time(&a, durationMs);
  lv_anim_set_delay(&a, delayMs);
  lv_anim_set_exec_cb(&a, anim_x_cb);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_repeat_delay(&a, 500);
  lv_anim_start(&a);
}

// ---- Idle/screensaver background: black bg, drifting butterflies + stars ----
void build_idle_animation_screen() {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, COL_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  lv_color_t colors[] = { COL_PINK, COL_LAVENDER, COL_MINT, COL_PEACH, COL_SKY };

  // A handful of butterflies drifting left-to-right at different heights/speeds
  for (int i = 0; i < 5; i++) {
    lv_obj_t *b = make_butterfly(scr, colors[i % 5]);
    int32_t y = 20 + i * 55;
    animate_flyer(b, -40, SCREEN_W + 40, y, 6000 + i * 800, i * 700);
  }

  // A few shooting stars, faster and staggered
  for (int i = 0; i < 4; i++) {
    lv_obj_t *s = make_star(scr, lv_color_white());
    int32_t y = 10 + i * 70;
    animate_flyer(s, -20, SCREEN_W + 20, y, 1800 + i * 300, i * 1200);
  }

  lv_scr_load(scr);
}

void setup() {
  Serial.begin(115200);

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SCREEN_W * 10);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_W;
  disp_drv.ver_res = SCREEN_H;
  disp_drv.flush_cb = tft_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  build_idle_animation_screen();

  // TODO: main menu screen (LEDs / Visualizers / Music) is separate and
  // swaps in on touch input, with this idle animation kicking back in
  // after ~45-60s of no input. Not built yet - this file only proves out
  // the animation piece.
}

void loop() {
  lv_timer_handler();
  delay(5);
}
