/**
 * 引脚定义头文件
 * ESP32-C6-DEV-KIT-N16 引脚分配
 */

#ifndef PINS_H
#define PINS_H

// SPI Bus (4屏共享)
#define PIN_SPI_MOSI    7
#define PIN_SPI_CLK     6

// LCD Chip Select (每屏独立)
#define PIN_LCD_CS0     8
#define PIN_LCD_CS1     9
#define PIN_LCD_CS2     10
#define PIN_LCD_CS3     11

// LCD Control (共享)
#define PIN_LCD_DC      4
#define PIN_LCD_RST     5
#define PIN_LCD_BL      3

// Key Inputs (每键独立, 内部上拉, 按下为LOW)
#define PIN_KEY0        0
#define PIN_KEY1        1
#define PIN_KEY2        2
#define PIN_KEY3        15

// EC11 Rotary Encoder
#define PIN_EC11_A      18
#define PIN_EC11_B      19
#define PIN_EC11_SW     20   // 按压开关

// Pin arrays for iteration
static const int LCD_CS_PINS[] = {PIN_LCD_CS0, PIN_LCD_CS1, PIN_LCD_CS2, PIN_LCD_CS3};
static const int KEY_PINS[] = {PIN_KEY0, PIN_KEY1, PIN_KEY2, PIN_KEY3};

#endif // PINS_H
