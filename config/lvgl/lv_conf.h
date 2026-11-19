/* LVGL configuration for the NBT Manager GUI. Options not set here keep the
 * defaults from third_party/lvgl/include/lvgl/config/lv_conf_internal.h. */
#ifndef LV_CONF_H
#define LV_CONF_H

/* 32-bit XRGB8888, matching the QNX Screen RGBX8888 window buffer. */
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_XRGB8888

/* Single-threaded use from the GUI main loop, with LVGL's own allocator. */
#define LV_USE_OS LV_OS_NONE
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (256 * 1024)

#define LV_USE_LOG 0
#define LV_USE_FLOAT 0
#define LV_USE_SYSMON 0
#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

/* Software rendering into XRGB8888 only. */
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_SUPPORT_RGB565 0
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 0
#define LV_DRAW_SW_SUPPORT_RGB565A8 0
#define LV_DRAW_SW_SUPPORT_RGB888 0
#define LV_DRAW_SW_SUPPORT_XRGB8888 1
#define LV_DRAW_SW_SUPPORT_ARGB8888 1
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
#define LV_DRAW_SW_SUPPORT_L8 0
#define LV_DRAW_SW_SUPPORT_AL88 0
#define LV_DRAW_SW_SUPPORT_A8 1
#define LV_DRAW_SW_SUPPORT_I1 1

/* Text uses the manager's own fonts, in its languages (src/gui/fonts/,
 * tools/make_fonts.py): the built-in Montserrat covers ASCII only. */
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_24 0
/* The pairing code (digits), as large as the phone shows it. */
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(nbt_font_latin_18)
#define LV_FONT_DEFAULT &nbt_font_latin_18

#endif
