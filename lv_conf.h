/**
 * LVGL Configuration File
 * Kiro Keyboard - 最小配置
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* 颜色配置 */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1

/* 内存配置 */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (48U * 1024U)

/* 显示配置 */
#define LV_DPI_DEF 130

/* 基本功能 */
#define LV_USE_LABEL 1
#define LV_USE_IMG 1
#define LV_USE_BTN 1
#define LV_USE_LINE 1
#define LV_USE_ARC 1

/* 禁用不需要的功能以节省内存 */
#define LV_USE_CHART 0
#define LV_USE_TABLE 0
#define LV_USE_CALENDAR 0
#define LV_USE_TEXTAREA 0
#define LV_USE_KEYBOARD 0
#define LV_USE_SPINBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_ROLLER 0
#define LV_USE_SLIDER 0
#define LV_USE_SWITCH 0
#define LV_USE_SPAN 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_MSGBOX 0
#define LV_USE_MENU 0

/* 字体 */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* 日志 */
#define LV_USE_LOG 0

/* 动画 */
#define LV_USE_ANIMATION 1

#endif /* LV_CONF_H */
