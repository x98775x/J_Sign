#if 1 /* Set this to "1" to enable the content */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Color depth: 1 (1 byte per pixel), 8, 16 or 32 */
#define LV_COLOR_DEPTH 16

/* Swap the 2 bytes of RGB565 color. Needed if display has different byte order */
#define LV_COLOR_16_SWAP 0

/* Enable features with 1/0 */
#define LV_COLOR_SCREEN_TRANSP 0

/* Pixels of the screen */
#define LV_HOR_RES_MAX  480
#define LV_VER_RES_MAX  320

/* Default display refresh period in milliseconds */
#define LV_DISP_DEF_REFR_PERIOD 30

/* Input device read period in milliseconds */
#define LV_INDEV_DEF_READ_PERIOD 30

/* Memory settings - chip has 96KB RAM, keep buffers small */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE   (24 * 1024U) /* 24KB for LVGL heap */

/* Garbage collector settings */
#define LV_ENABLE_GC 0

/* Image cache size - 0 means no caching */
#define LV_IMG_CACHE_DEF_SIZE 0

/* Maximum buffer size to allocate for rotation */
#define LV_DISP_ROT_MAX_BUF (10*1024)

/* 1: Enable the Animations */
#define LV_USE_ANIMATION 1

/* 1: Enable shadow drawing */
#define LV_DRAW_COMPLEX 1

/* Default Montserrat font - keep only what we need to save flash */
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_12_SUBPX      0
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_28_COMPRESSED 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
#define LV_FONT_SIMSUN_16_CJK            0
#define LV_FONT_UNSCII_8                 0
#define LV_FONT_UNSCII_16                0

#define LV_FONT_DEFAULT &lv_font_montserrat_16

/* Enable widgets we actually use */
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX  0
#define LV_USE_CANVAS     0
#define LV_USE_CHECKBOX   0
#define LV_USE_DROPDOWN   1
#define LV_USE_IMG        1
#define LV_USE_LABEL      1
#define LV_USE_LINE       1
#define LV_USE_ROLLER     1
#define LV_USE_SLIDER     1
#define LV_USE_SWITCH     0
#define LV_USE_TEXTAREA   0
#define LV_USE_TABLE      0

/* Extra widgets */
#define LV_USE_ANIMIMG    0
#define LV_USE_CALENDAR   0
#define LV_USE_CHART      0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN     0
#define LV_USE_KEYBOARD   0
#define LV_USE_LED        0
#define LV_USE_LIST       1
#define LV_USE_MENU       0
#define LV_USE_METER      0
#define LV_USE_MSGBOX     0
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    1
#define LV_USE_TABVIEW    0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0

/* Themes */
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_THEME_BASIC   0
#define LV_USE_THEME_MONO    0

/* Layouts */
#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/* Logging - disable for production */
#define LV_USE_LOG 0

/* Asserts */
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

#endif /* LV_CONF_H */
#endif /* End of "Content enable" */
