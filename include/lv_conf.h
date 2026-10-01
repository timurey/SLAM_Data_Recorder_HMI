/**
 * LVGL 8 configuration for CYD ESP32-2432S028R (240×320 ST7789).
 * Only BTN, LABEL, BAR, QRCODE and Flex layout are enabled.
 */
#if 1  /* Set to 0 to disable the whole file */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Color depth */
#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP 0

/* Memory */
#define LV_MEM_CUSTOM      0
#define LV_MEM_SIZE        (48U * 1024U)

/* HAL tick — use Arduino millis() */
#define LV_TICK_CUSTOM     1
#define LV_TICK_CUSTOM_INCLUDE  "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR  (millis())

/* Logging / asserts */
#define LV_USE_LOG          0
#define LV_USE_ASSERT_NULL  1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_OBJ   0
#define LV_USE_ASSERT_STYLE 0

/* ── Fonts ─────────────────────────────────────────────────────────────────── */
#define LV_FONT_MONTSERRAT_8   0
#define LV_FONT_MONTSERRAT_10  0
#define LV_FONT_MONTSERRAT_12  1
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_18  0
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_22  0
#define LV_FONT_MONTSERRAT_24  1
#define LV_FONT_MONTSERRAT_26  0
#define LV_FONT_MONTSERRAT_28  0
#define LV_FONT_MONTSERRAT_30  0
#define LV_FONT_MONTSERRAT_32  0
#define LV_FONT_MONTSERRAT_34  0
#define LV_FONT_MONTSERRAT_36  0
#define LV_FONT_MONTSERRAT_38  0
#define LV_FONT_MONTSERRAT_40  0
#define LV_FONT_MONTSERRAT_42  0
#define LV_FONT_MONTSERRAT_44  0
#define LV_FONT_MONTSERRAT_46  0
#define LV_FONT_MONTSERRAT_48  0
#define LV_FONT_DEFAULT        &lv_font_montserrat_16

/* ── Base widgets ───────────────────────────────────────────────────────────── */
#define LV_USE_ARC         0
#define LV_USE_BAR         1
#define LV_USE_BTN         1
#define LV_USE_BTNMATRIX   0   /* disabled → also disable keyboard below */
#define LV_USE_CANVAS      0
#define LV_USE_CHECKBOX    0
#define LV_USE_DROPDOWN    0
#define LV_USE_IMG         0   /* disabled → also disable animimg, imgbtn below */
#define LV_USE_LABEL       1
#define LV_USE_LINE        0
#define LV_USE_ROLLER      0
#define LV_USE_SLIDER      0
#define LV_USE_SWITCH      0
#define LV_USE_TEXTAREA    0   /* disabled → also disable keyboard below */
#define LV_USE_TABLE       0

/* ── Extra widgets (all off except qrcode) ─────────────────────────────────── */
#define LV_USE_ANIMIMG     0   /* needs LV_USE_IMG */
#define LV_USE_CALENDAR    0
#define LV_USE_CHART       0
#define LV_USE_COLORWHEEL  0
#define LV_USE_IMGBTN      0   /* needs LV_USE_IMG */
#define LV_USE_KEYBOARD    0   /* needs LV_USE_BTNMATRIX + LV_USE_TEXTAREA */
#define LV_USE_LED         0
#define LV_USE_LIST        0
#define LV_USE_MENU        0
#define LV_USE_METER       0
#define LV_USE_MSGBOX      0
#define LV_USE_QRCODE      1   /* ← only extra widget we need */
#define LV_USE_SPINBOX     0
#define LV_USE_SPINNER     0
#define LV_USE_TABVIEW     0
#define LV_USE_TILEVIEW    0
#define LV_USE_WIN         0
#define LV_USE_SPAN        0

/* ── Animation ──────────────────────────────────────────────────────────────── */
#define LV_USE_ANIMATION   1

/* ── Themes ─────────────────────────────────────────────────────────────────── */
#define LV_USE_THEME_DEFAULT  1
#define LV_THEME_DEFAULT_DARK 1
#define LV_USE_THEME_BASIC    0
#define LV_USE_THEME_MONO     0

/* ── Layouts ────────────────────────────────────────────────────────────────── */
#define LV_USE_FLEX  1
#define LV_USE_GRID  0

/* ── Drawing ────────────────────────────────────────────────────────────────── */
#define LV_DRAW_COMPLEX      1
#define LV_SHADOW_CACHE_SIZE 0

#endif /* LV_CONF_H */
#endif /* End of file enable */
