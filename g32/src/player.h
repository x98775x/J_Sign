#pragma once
#include "compat.h"
#include "ff.h"

/* ============================================================
 * Music player: the SCREEN reads the SD card (FatFs) and streams the file to the ESP32,
 * which decodes it and plays it through the NS4168.
 *
 *   GD32 -> ESP32 : PLAY:<id>:<size>:<mp3|wav>   start a new stream
 *                   STOP | PAUSE | RESUME | VOL:n
 *                   D:<id>:<offset>:<len>:<sum>\n<len raw bytes>    answer to a READ
 *   ESP32 -> GD32 : READ:<id>:<offset>:<len>     "send me this part of the file"
 *                   ENDED:<id>                   finished decoding this stream
 *                   READY                        ESP32 (re)booted
 * ============================================================ */

void send_cmd(const char *cmd);
void music_refresh_ui(void);
void sd_invalidate(void);

#define PL_MAX      48
#define PL_NAME_LEN 64
#define PL_CHUNK    2048

static FATFS    pl_fs;
static FIL      pl_fil;
static char     pl_name[PL_MAX][PL_NAME_LEN];
static int      pl_count   = 0;
static int      pl_idx     = 0;
static bool     pl_scanned = false;
static bool     pl_sd_ok   = false;
static bool     pl_playing = false;      /* the user wants sound */
static bool     pl_open    = false;      /* a file is open and the ESP32 has been told to play it */
static uint32_t pl_id      = 0;
static uint32_t pl_size    = 0;
static bool     req_pending = false;
static uint32_t req_off, req_len;
static uint8_t  pl_buf[PL_CHUNK];

static bool pl_ext_is(const char *name, const char *ext) {
  size_t n = strlen(name), e = strlen(ext);
  if (n < e) return false;
  for (size_t i = 0; i < e; i++) {
    char a = name[n - e + i], b = ext[i];
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

static int pl_cmp(const char *a, const char *b) {
  for (;; a++, b++) {
    char x = *a, y = *b;
    if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
    if (x != y) return (x < y) ? -1 : 1;
    if (x == 0) return 0;
  }
}

/* Title for the screen: file name without the extension. */
static void pl_title(int idx, char *out, size_t outsz) {
  if (idx < 0 || idx >= pl_count) { out[0] = '\0'; return; }
  strncpy(out, pl_name[idx], outsz - 1);
  out[outsz - 1] = '\0';
  char *dot = strrchr(out, '.');
  if (dot) *dot = '\0';
}

static void pl_close(void) {
  if (pl_open) {
    f_close(&pl_fil);
    pl_open = false;
    send_cmd("STOP");
  }
  req_pending = false;
}

static void pl_scan(void) {
  char keep[PL_NAME_LEN] = "";
  if (pl_count > 0 && pl_idx < pl_count) strncpy(keep, pl_name[pl_idx], PL_NAME_LEN - 1);

  pl_count = 0;
  pl_sd_ok = false;
  pl_scanned = true;
  f_mount(NULL, "", 0);
  sd_invalidate();
  if (f_mount(&pl_fs, "", 1) != FR_OK) return;
  pl_sd_ok = true;

  DIR dir;
  FILINFO fno;
  if (f_opendir(&dir, "/") != FR_OK) return;
  while (pl_count < PL_MAX && f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
    if (fno.fattrib & (AM_DIR | AM_HID | AM_SYS)) continue;
    if (fno.fname[0] == '.') continue;
    size_t l = strlen(fno.fname);
    if (l < 5 || l >= PL_NAME_LEN) continue;
    if (!pl_ext_is(fno.fname, ".mp3") && !pl_ext_is(fno.fname, ".wav")) continue;
    strcpy(pl_name[pl_count++], fno.fname);
  }
  f_closedir(&dir);

  for (int i = 1; i < pl_count; i++) {          /* alphabetical */
    char tmp[PL_NAME_LEN];
    strcpy(tmp, pl_name[i]);
    int j = i - 1;
    while (j >= 0 && pl_cmp(pl_name[j], tmp) > 0) { strcpy(pl_name[j + 1], pl_name[j]); j--; }
    strcpy(pl_name[j + 1], tmp);
  }
  pl_idx = 0;                                    /* stay on the same song after a rescan */
  for (int i = 0; i < pl_count; i++) if (strcmp(pl_name[i], keep) == 0) { pl_idx = i; break; }
}

/* Open song idx and tell the ESP32 to start decoding it. step = +1/-1 for skipping unreadable files. */
static bool pl_start(int idx, int step) {
  pl_close();
  if (!pl_sd_ok || pl_count == 0) return false;
  for (int tries = 0; tries < pl_count; tries++) {
    pl_idx = ((idx % pl_count) + pl_count) % pl_count;
    char path[PL_NAME_LEN + 2];
    snprintf(path, sizeof(path), "/%s", pl_name[pl_idx]);
    if (f_open(&pl_fil, path, FA_READ) == FR_OK) {
      pl_size = (uint32_t)f_size(&pl_fil);
      pl_id++;
      pl_open = true;
      char cmd[64];
      snprintf(cmd, sizeof(cmd), "PLAY:%u:%u:%s", (unsigned)pl_id, (unsigned)pl_size,
               pl_ext_is(pl_name[pl_idx], ".wav") ? "wav" : "mp3");
      send_cmd(cmd);
      return true;
    }
    idx += step;
  }
  return false;
}

/* ---- controls used by the UI ---- */
static void player_open_screen(void) {
  bool was_playing = pl_playing;
  pl_close();                                  /* release the old card before remounting */
  pl_playing = false;
  pl_scan();                                   /* always pick up a swapped card */
  if (was_playing) pl_playing = pl_start(pl_idx, +1);
  music_refresh_ui();
}

static void player_play(void) {
  if (pl_open) { pl_playing = true; send_cmd("RESUME"); }
  else         { pl_playing = pl_start(pl_idx, +1); }
  music_refresh_ui();
}

static void player_pause(void) {
  pl_playing = false;
  if (pl_open) send_cmd("PAUSE");
  music_refresh_ui();
}

static void player_step(int step) {              /* +1 next, -1 previous */
  if (pl_count == 0) return;
  int target = pl_idx + step;
  if (pl_playing) {
    pl_playing = pl_start(target, step);
  } else {
    pl_close();
    pl_idx = ((target % pl_count) + pl_count) % pl_count;
  }
  music_refresh_ui();
}

/* ---- serve the ESP32's READ requests ---- */
static void player_tick(void) {
  if (!req_pending) return;
  if (!pl_open) { req_pending = false; return; }
  if (hal_uart_tx_free() < PL_CHUNK + 64u) return;       /* wait until the whole chunk fits */

  uint32_t off = req_off, len = req_len;
  req_pending = false;
  if (len > PL_CHUNK) len = PL_CHUNK;

  UINT br = 0;
  if (off < pl_size) {
    if (f_tell(&pl_fil) != off) f_lseek(&pl_fil, off);
    if (f_read(&pl_fil, pl_buf, len, &br) != FR_OK) br = 0;   /* read error = treat as end of file */
  }
  uint32_t sum = 0;
  for (UINT i = 0; i < br; i++) sum += pl_buf[i];
  sum &= 0xFFFFu;

  char hdr[56];
  int n = snprintf(hdr, sizeof(hdr), "D:%u:%u:%u:%u\n", (unsigned)pl_id, (unsigned)off, (unsigned)br, (unsigned)sum);
  hal_uart_write((const uint8_t *)hdr, (uint32_t)n);
  hal_uart_write(pl_buf, br);
}

/* ---- lines from the ESP32 ---- */
static const char *pl_num(const char *p, uint32_t *out) {
  uint32_t v = 0;
  while (*p >= '0' && *p <= '9') { v = v * 10u + (uint32_t)(*p - '0'); p++; }
  *out = v;
  return (*p == ':') ? p + 1 : p;
}

static void player_handle_line(const char *l) {
  if (strncmp(l, "READ:", 5) == 0) {
    uint32_t id, off, len;
    const char *p = pl_num(l + 5, &id);
    p = pl_num(p, &off);
    pl_num(p, &len);
    if (pl_open && id == pl_id) { req_off = off; req_len = len; req_pending = true; }
  } else if (strncmp(l, "ENDED:", 6) == 0) {
    uint32_t id;
    pl_num(l + 6, &id);
    if (pl_open && id == pl_id) {                /* song finished: next one, forever */
      pl_close();
      if (pl_playing) pl_playing = pl_start(pl_idx + 1, +1);
      music_refresh_ui();
    }
  } else if (strcmp(l, "READY") == 0) {          /* ESP32 rebooted: its stream is gone */
    pl_close();
    if (pl_playing) pl_playing = pl_start(pl_idx, +1);
    music_refresh_ui();
  }
}
