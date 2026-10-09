// LVGL 8.3 configuration. Only non-default values are set here;
// everything else falls back to lvgl/src/lv_conf_internal.h.
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

// RGB565, native byte order (LovyanGFX swaps when writing to the panel)
#define LV_COLOR_DEPTH   16
#define LV_COLOR_16_SWAP 0

// Fixed internal pool — no malloc() churn from LVGL at runtime
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE   (64U * 1024U)

#define LV_DISP_DEF_REFR_PERIOD 30
#define LV_INDEV_DEF_READ_PERIOD 30

#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 0
#define LV_THEME_DEFAULT_TRANSITION_TIME 0

#endif  // LV_CONF_H
#endif
