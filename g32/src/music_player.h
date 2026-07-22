#pragma once
#include <Arduino.h>
#include <SdFat.h>
#include <lvgl.h>

// ============================================================
// SD card pin mapping (per BTT TFT35 pinout)
//   CS=PA4  MOSI=PA7  CLK=PA5  MISO=PA6  DET=PC4
//
// Audio output hardware - NOT WIRED YET.
// When speaker/amp is added, wire to a PWM-capable pin and
// use analogWrite() with a timer for basic PWM audio, or add
// an I2S DAC (e.g. MAX98357A) for better quality.
// This file handles SD file listing now; playback hooks are
// stubbed for when the amp arrives.
// ============================================================

#define SD_CS   PA4
#define SD_MOSI PA7
#define SD_CLK  PA5
#define SD_MISO PA6
#define SD_DET  PC4

// Max files to show in the music list
#define MAX_MUSIC_FILES 32

static SdFat   sd;
static bool    sd_ready       = false;
static char    music_files[MAX_MUSIC_FILES][64];
static int     music_count    = 0;
static int     music_sel      = 0;
static bool    music_playing  = false;

// ---- Init SD card ----
bool music_sd_init() {
    if (!sd.begin(SdSpiConfig(SD_CS, DEDICATED_SPI, SD_SCK_MHZ(18)))) {
        sd_ready = false;
        return false;
    }
    sd_ready = true;
    return true;
}

// ---- Scan SD root for .mp3/.wav files ----
int music_scan_files() {
    if (!sd_ready) return 0;
    music_count = 0;

    SdFile root;
    if (!root.open("/")) return 0;

    SdFile entry;
    while (entry.openNext(&root, O_RDONLY) && music_count < MAX_MUSIC_FILES) {
        char name[64];
        entry.getName(name, sizeof(name));
        // Simple extension check
        int len = strlen(name);
        if (len > 4) {
            char *ext = name + len - 4;
            if (strcasecmp(ext, ".mp3") == 0 || strcasecmp(ext, ".wav") == 0) {
                strncpy(music_files[music_count], name, 63);
                music_files[music_count][63] = '\0';
                music_count++;
            }
        }
        entry.close();
    }
    root.close();
    return music_count;
}

// ---- Playback stubs (fill in when amp hardware arrives) ----
void music_play(int idx) {
    if (!sd_ready || idx < 0 || idx >= music_count) return;
    music_sel = idx;
    music_playing = true;
    // TODO: open music_files[idx] and stream to audio output
    // For PWM audio: read PCM samples, write via analogWrite to speaker pin
    // For I2S DAC: configure I2S peripheral and DMA, stream frames
}

void music_stop() {
    music_playing = false;
    // TODO: stop DMA/timer, close file
}

void music_next() {
    if (music_count == 0) return;
    music_sel = (music_sel + 1) % music_count;
    if (music_playing) music_play(music_sel);
}

void music_prev() {
    if (music_count == 0) return;
    music_sel = (music_sel - 1 + music_count) % music_count;
    if (music_playing) music_play(music_sel);
}

const char* music_current_name() {
    if (music_count == 0 || music_sel < 0) return "No files";
    return music_files[music_sel];
}

// ============================================================
// LVGL Music Screen UI objects (referenced from main.cpp)
// ============================================================
static lv_obj_t *music_list     = NULL;
static lv_obj_t *lbl_now        = NULL;
static lv_obj_t *lbl_status     = NULL;

void music_refresh_ui();

static void music_list_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    music_play(idx);
    music_refresh_ui();
}
static void music_playpause_cb(lv_event_t *e) {
    if (music_playing) music_stop();
    else music_play(music_sel);
    music_refresh_ui();
}
static void music_next_cb(lv_event_t *e) { music_next(); music_refresh_ui(); }
static void music_prev_cb(lv_event_t *e) { music_prev(); music_refresh_ui(); }

void music_refresh_ui() {
    if (!lbl_now || !lbl_status) return;
    lv_label_set_text(lbl_now, music_current_name());
    lv_label_set_text(lbl_status,
        music_playing ? LV_SYMBOL_PAUSE " Playing" : LV_SYMBOL_PLAY " Stopped");
}

// Build the full music screen content (call once, pass the screen obj)
void music_build_ui(lv_obj_t *scr,
                    lv_event_cb_t back_cb,
                    lv_color_t col_peach,
                    lv_color_t col_lavender,
                    lv_color_t col_dark,
                    lv_color_t col_white,
                    lv_color_t col_mint) {

    // Try to init SD
    bool sd_ok = music_sd_init();
    int  count = sd_ok ? music_scan_files() : 0;

    // Now playing label
    lbl_now = lv_label_create(scr);
    lv_label_set_text(lbl_now, sd_ok ? (count > 0 ? music_files[0] : "No MP3/WAV found") : "No SD card");
    lv_obj_set_style_text_color(lbl_now, col_white, 0);
    lv_obj_set_style_text_font(lbl_now, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(lbl_now, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_now, 380);
    lv_obj_align(lbl_now, LV_ALIGN_TOP_MID, 0, 55);

    // Status label
    lbl_status = lv_label_create(scr);
    lv_label_set_text(lbl_status, LV_SYMBOL_PLAY " Stopped");
    lv_obj_set_style_text_color(lbl_status, col_peach, 0);
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_status, LV_ALIGN_TOP_MID, 0, 75);

    // Prev / Play-Pause / Next controls
    // -- Prev --
    lv_obj_t *btn_prev = lv_btn_create(scr);
    lv_obj_set_size(btn_prev, 60, 45);
    lv_obj_align(btn_prev, LV_ALIGN_BOTTOM_MID, -80, -15);
    lv_obj_set_style_bg_color(btn_prev, col_lavender, 0);
    lv_obj_set_style_radius(btn_prev, 10, 0);
    lv_obj_set_style_border_width(btn_prev, 0, 0);
    lv_obj_add_event_cb(btn_prev, music_prev_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_prev = lv_label_create(btn_prev);
    lv_label_set_text(lbl_prev, LV_SYMBOL_PREV);
    lv_obj_set_style_text_color(lbl_prev, col_dark, 0);
    lv_obj_center(lbl_prev);

    // -- Play/Pause --
    lv_obj_t *btn_pp = lv_btn_create(scr);
    lv_obj_set_size(btn_pp, 80, 55);
    lv_obj_align(btn_pp, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_set_style_bg_color(btn_pp, col_peach, 0);
    lv_obj_set_style_radius(btn_pp, 12, 0);
    lv_obj_set_style_border_width(btn_pp, 0, 0);
    lv_obj_add_event_cb(btn_pp, music_playpause_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_pp = lv_label_create(btn_pp);
    lv_label_set_text(lbl_pp, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(lbl_pp, col_dark, 0);
    lv_obj_center(lbl_pp);

    // -- Next --
    lv_obj_t *btn_next = lv_btn_create(scr);
    lv_obj_set_size(btn_next, 60, 45);
    lv_obj_align(btn_next, LV_ALIGN_BOTTOM_MID, 80, -15);
    lv_obj_set_style_bg_color(btn_next, col_lavender, 0);
    lv_obj_set_style_radius(btn_next, 10, 0);
    lv_obj_set_style_border_width(btn_next, 0, 0);
    lv_obj_add_event_cb(btn_next, music_next_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_next = lv_label_create(btn_next);
    lv_label_set_text(lbl_next, LV_SYMBOL_NEXT);
    lv_obj_set_style_text_color(lbl_next, col_dark, 0);
    lv_obj_center(lbl_next);

    // File list (scrollable)
    if (count > 0) {
        music_list = lv_list_create(scr);
        lv_obj_set_size(music_list, 440, 130);
        lv_obj_align(music_list, LV_ALIGN_CENTER, 0, 10);
        lv_obj_set_style_bg_color(music_list, col_dark, 0);
        lv_obj_set_style_border_color(music_list, col_lavender, 0);
        lv_obj_set_style_border_width(music_list, 1, 0);
        lv_obj_set_style_radius(music_list, 8, 0);

        for (int i = 0; i < count; i++) {
            lv_obj_t *btn = lv_list_add_btn(music_list, LV_SYMBOL_AUDIO, music_files[i]);
            lv_obj_set_style_bg_color(btn, col_dark, 0);
            lv_obj_set_style_text_color(btn, col_white, 0);
            lv_obj_add_event_cb(btn, music_list_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        }
    } else {
        lv_obj_t *no_files = lv_label_create(scr);
        lv_label_set_text(no_files,
            sd_ok ? "No MP3 or WAV files found on SD card.\nAdd files and restart."
                  : "No SD card detected.\nInsert one and restart.");
        lv_obj_set_style_text_color(no_files, col_white, 0);
        lv_obj_set_style_text_font(no_files, &lv_font_montserrat_14, 0);
        lv_label_set_long_mode(no_files, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(no_files, 360);
        lv_obj_set_style_text_align(no_files, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(no_files, LV_ALIGN_CENTER, 0, 0);
    }
}
