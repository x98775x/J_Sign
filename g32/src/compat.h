#pragma once
/* Tiny Arduino-ism shims so the original JULIA UI code builds as plain C on GD32 SPL. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "lvgl.h"
#include "hal.h"

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

/* Use LVGL's own snprintf so no libc malloc/_sbrk dependencies get pulled in. */
#define snprintf lv_snprintf
