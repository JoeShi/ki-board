/**
 * display.h - 4个 ScreenKey 屏幕管理
 *
 * 4个 Waveshare 0.85inch ScreenKey (ST7735, 128x128) 共享 SPI 总线,
 * 共享 DC/RST/BL, 各自独立 CS。每个屏幕显示对应按键的功能标签。
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include "config.h"

// 初始化所有屏幕 (返回成功初始化的屏幕数)
uint8_t displayBegin();

// === 背光 PWM 调光 ===

// 设置背光亮度 (0=关, 255=最亮)
void displaySetBacklight(uint8_t level);

// 获取当前背光亮度
uint8_t displayGetBacklight();

// 在指定屏幕显示按键功能
//   index: 屏幕索引 0..NUM_KEYS-1
//   label: 功能名称
//   color: 文字颜色 (RGB565)
void displayShowKey(uint8_t index, const char* label, uint16_t color);

// 显示按键被按下的视觉反馈 (短暂高亮)
void displayFlashKey(uint8_t index);

// 在所有屏幕上显示当前旋钮模式提示 (例如顶部一行)
void displayShowEncoderMode(const char* modeLabel);

// 常用颜色 (RGB565)
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_ORANGE  0xFD20

#endif // DISPLAY_H
