#pragma once
#include "compat.h"
#include "player.h"

/* Music page: Back, prev, play/pause, next, volume. The song list lives on the SD card in the screen. */

void show_menu(void);
void send_cmd(const char *cmd);

#define MUSIC_FOCUS_COUNT 5   /* back, prev, play/pause, next, volume */

static bool esp_ready = false;
static int  music_vol = 60;               /* 0..100 */

static lv_obj_t *music_btn_back = NULL;
static lv_obj_t *music_btn_prev = NULL;
static lv_obj_t *music_btn_pp   = NULL;
static lv_obj_t *music_btn_next = NULL;
static lv_obj_t *music_slider   = NULL;
static lv_obj_t *lbl_now        = NULL;
static lv_obj_t *lbl_status     = NULL;
static lv_obj_t *lbl_vol        = NULL;
static lv_obj_t *lbl_pp         = NULL;

static lv_obj_t *music_focus_obj[MUSIC_FOCUS_COUNT];
static int      music_focus   = 0;
static bool     music_editing = false;
static uint32_t music_vol_sent_ms = 0;
static bool music_vol_pending = false;

static lv_color_t mcol_dark;

void music_refresh_ui(void) {
  if (!lbl_now) return;
  char buf[96], title[PL_NAME_LEN];

  if (pl_count > 0) pl_title(pl_idx, title, sizeof(title));
  else              strcpy(title, "-");
  lv_label_set_text(lbl_now, title);

  if (!pl_sd_ok)           snprintf(buf, sizeof(buf), "No SD card in the screen");
  else if (pl_count == 0)  snprintf(buf, sizeof(buf), "No MP3 / WAV files on the card");
  else if (!esp_ready)     snprintf(buf, sizeof(buf), "%d/%d   Waiting for audio board...", pl_idx + 1, pl_count);
  else                     snprintf(buf, sizeof(buf), "%d/%d   %s", pl_idx + 1, pl_count,
                                    pl_playing ? LV_SYMBOL_PLAY " Playing" : LV_SYMBOL_PAUSE " Paused");
  lv_label_set_text(lbl_status, buf);

  lv_label_set_text(lbl_pp, pl_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
  snprintf(buf, sizeof(buf), LV_SYMBOL_VOLUME_MAX " Volume: %d", music_vol);
  lv_label_set_text(lbl_vol, buf);
  lv_slider_set_value(music_slider, music_vol, LV_ANIM_OFF);
}

static void music_highlight(void) {
  music_focus_obj[0] = music_btn_back;
  music_focus_obj[1] = music_btn_prev;
  music_focus_obj[2] = music_btn_pp;
  music_focus_obj[3] = music_btn_next;
  music_focus_obj[4] = music_slider;
  for (int i = 0; i < MUSIC_FOCUS_COUNT; i++) {
    if (!music_focus_obj[i]) continue;
    bool on = (i == music_focus);
    lv_obj_set_style_border_width(music_focus_obj[i], on ? 3 : 0, 0);
    lv_obj_set_style_border_color(music_focus_obj[i],
        (on && music_editing) ? lv_color_hex(0xAAFFC3) : lv_color_hex(0xFFFFFF), 0);
  }
}

static void music_send_vol(bool force) {
  music_vol_pending = true;
  if (!force && (millis() - music_vol_sent_ms) < 100u) return;
  char buf[16];
  snprintf(buf, sizeof(buf), "VOL:%d", music_vol);
  send_cmd(buf);
  music_vol_sent_ms = millis();
  music_vol_pending = false;
}

static void music_tick(void) {
  if (music_vol_pending) music_send_vol(false);
}

static void music_do_playpause(void) {
  if (pl_playing) player_pause();
  else            player_play();
}

static void music_pp_cb(lv_event_t *e)   { (void)e; music_do_playpause(); }
static void music_next_cb(lv_event_t *e) { (void)e; player_step(+1); }
static void music_prev_cb(lv_event_t *e) { (void)e; player_step(-1); }
static void music_slider_cb(lv_event_t *e) {
  music_vol = lv_slider_get_value(lv_event_get_target(e));
  music_send_vol(lv_event_get_code(e) == LV_EVENT_RELEASED);
  music_refresh_ui();
}

/* Lines from the ESP32 that matter to this page. */
static void music_handle_line(const char *l) {
  if (strcmp(l, "READY") == 0) {
    esp_ready = true;
    music_send_vol(true);                     /* the ESP32 forgot our volume when it rebooted */
    music_refresh_ui();
  }
}

static lv_obj_t *music_mkbtn(lv_obj_t *scr, const char *txt, lv_color_t bg,
                             int w, int h, int x, int y,
                             lv_event_cb_t cb, lv_obj_t **label_out) {
  lv_obj_t *b = lv_btn_create(scr);
  lv_obj_set_size(b, w, h);
  lv_obj_align(b, LV_ALIGN_BOTTOM_MID, x, y);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_radius(b, 12, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_color(l, mcol_dark, 0);
  lv_obj_center(l);
  if (label_out) *label_out = l;
  return b;
}

/* Everything except the Back button / title (main.c owns those). */
static void music_build_ui(lv_obj_t *scr, lv_event_cb_t back_cb,
                           lv_color_t col_peach, lv_color_t col_lavender,
                           lv_color_t col_dark, lv_color_t col_white, lv_color_t col_mint) {
  (void)back_cb; (void)col_mint;
  mcol_dark = col_dark;

  lbl_now = lv_label_create(scr);
  lv_obj_set_style_text_color(lbl_now, col_white, 0);
  lv_obj_set_style_text_font(lbl_now, &lv_font_montserrat_20, 0);
  lv_label_set_long_mode(lbl_now, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(lbl_now, 420);
  lv_obj_set_style_text_align(lbl_now, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl_now, LV_ALIGN_TOP_MID, 0, 80);

  lbl_status = lv_label_create(scr);
  lv_obj_set_style_text_color(lbl_status, col_peach, 0);
  lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_16, 0);
  lv_obj_align(lbl_status, LV_ALIGN_TOP_MID, 0, 116);

  lbl_vol = lv_label_create(scr);
  lv_obj_set_style_text_color(lbl_vol, col_white, 0);
  lv_obj_set_style_text_font(lbl_vol, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_vol, LV_ALIGN_TOP_MID, 0, 160);

  music_slider = lv_slider_create(scr);
  lv_slider_set_range(music_slider, 0, 100);
  lv_obj_set_size(music_slider, 320, 16);
  lv_obj_align(music_slider, LV_ALIGN_TOP_MID, 0, 190);
  lv_obj_set_style_bg_color(music_slider, col_peach, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(music_slider, col_peach, LV_PART_KNOB);
  lv_obj_add_event_cb(music_slider, music_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(music_slider, music_slider_cb, LV_EVENT_RELEASED, NULL);

  music_btn_prev = music_mkbtn(scr, LV_SYMBOL_PREV, col_lavender, 70, 50, -95, -14, music_prev_cb, NULL);
  music_btn_pp   = music_mkbtn(scr, LV_SYMBOL_PLAY, col_peach,    90, 58,   0, -14, music_pp_cb, &lbl_pp);
  music_btn_next = music_mkbtn(scr, LV_SYMBOL_NEXT, col_lavender, 70, 50,  95, -14, music_next_cb, NULL);

  music_refresh_ui();
}

void music_set_back_btn(lv_obj_t *back) {
  music_btn_back = back;
  music_focus = 0;
  music_editing = false;
  music_highlight();
  player_open_screen();          /* (re)read the SD card and show the current song */
}

void music_encoder_rotate(int delta) {
  if (music_editing) {
    music_vol = constrain(music_vol + delta * 5, 0, 100);
    music_send_vol(false);
    music_refresh_ui();
    return;
  }
  music_focus = (music_focus + delta + MUSIC_FOCUS_COUNT) % MUSIC_FOCUS_COUNT;
  music_highlight();
}

void music_encoder_click(void) {
  if (music_editing) {            /* click again = done adjusting, send the final value */
    music_editing = false;
    music_send_vol(true);
    music_highlight();
    return;
  }
  switch (music_focus) {
    case 0: show_menu();                              break;
    case 1: player_step(-1);                          break;
    case 2: music_do_playpause();                     break;
    case 3: player_step(+1);                          break;
    case 4: music_editing = true; music_highlight();  break;
  }
}
