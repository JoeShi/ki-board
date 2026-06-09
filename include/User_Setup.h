/**
 * TFT_eSPI 用户配置文件
 * 针对 Waveshare 0.85inch ScreenKey Module B (ST7735, 128x128)
 * 主控: ESP32-C6
 * 
 * 注意: 此文件需要在 platformio.ini 的 build_flags 中指定路径
 */

#ifndef USER_SETUP_H
#define USER_SETUP_H

// ==================== ESP32-C6 兼容性修复 ====================
// ESP32-C6 没有 VSPI/HSPI，只有 SPI2_HOST
#ifndef VSPI
#define VSPI SPI2_HOST
#endif
#ifndef HSPI
#define HSPI SPI2_HOST
#endif

// ==================== 驱动选择 ====================
#define ST7735_DRIVER

// 0.85inch ScreenKey 使用 128x128 尺寸
#define TFT_WIDTH  128
#define TFT_HEIGHT 128

// ST7735 Tab 类型 (根据实际屏幕调整)
#define ST7735_GREENTAB128

// ==================== ESP32-C6 引脚定义 ====================
#define TFT_MOSI    7
#define TFT_SCLK    6
#define TFT_CS      8    // 默认使用CS0, 多屏时需要手动切换
#define TFT_DC      4
#define TFT_RST     5
#define TFT_BL      3

// ==================== SPI 频率 ====================
#define SPI_FREQUENCY       27000000  // 27MHz SPI clock
#define SPI_READ_FREQUENCY  16000000
#define SPI_TOUCH_FREQUENCY  2500000

// ==================== 字体 ====================
#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font
#define LOAD_FONT2  // Font 2. Small 16 pixel high font
#define LOAD_FONT4  // Font 4. Medium 26 pixel high font
#define LOAD_GFXFF  // FreeFonts

#define SMOOTH_FONT

#endif // USER_SETUP_H
