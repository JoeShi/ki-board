/**
 * Pin definitions for ESP32-S3 + PCB ESP32_5SCREEN_V0.1
 * 3x ScreenKey (SPI-A) + 0.71" round LCD + 1.47" rect LCD (SPI-B)
 */

#ifndef PINS_H
#define PINS_H

// === SPI Bus A: 3x ScreenKey (ST7735, shared MOSI/CLK) ===
#define PIN_SPIA_MOSI   9
#define PIN_SPIA_CLK    10

// ScreenKey 1 (J1)
#define PIN_SK1_CS      13
#define PIN_SK1_DC      18
#define PIN_SK1_RST     4
#define PIN_SK1_BL      42
#define PIN_SK1_KEY     1

// ScreenKey 2 (J11)
#define PIN_SK2_CS      14
#define PIN_SK2_DC      8
#define PIN_SK2_RST     38
#define PIN_SK2_BL      43
#define PIN_SK2_KEY     2

// ScreenKey 3 (J12)
#define PIN_SK3_CS      15
#define PIN_SK3_DC      7
#define PIN_SK3_RST     39
#define PIN_SK3_BL      44
#define PIN_SK3_KEY     21

// === SPI Bus B: Round LCD + Rect LCD (shared MOSI/CLK) ===
#define PIN_SPIB_MOSI   12
#define PIN_SPIB_CLK    11

// Round LCD 0.71" (J5, GC9D01, 160x160)
#define PIN_RLCD_CS     16
#define PIN_RLCD_DC     6
#define PIN_RLCD_RST    40
#define PIN_RLCD_BL     47

// Rect LCD 1.47" (J6, ST7789V3, 172x320)
#define PIN_RECT_CS     17
#define PIN_RECT_DC     5
#define PIN_RECT_RST    41
#define PIN_RECT_BL     48

// === Pin arrays for iteration ===
static const int SK_CS_PINS[]  = {PIN_SK1_CS, PIN_SK2_CS, PIN_SK3_CS};
static const int SK_DC_PINS[]  = {PIN_SK1_DC, PIN_SK2_DC, PIN_SK3_DC};
static const int SK_RST_PINS[] = {PIN_SK1_RST, PIN_SK2_RST, PIN_SK3_RST};
static const int SK_BL_PINS[]  = {PIN_SK1_BL, PIN_SK2_BL, PIN_SK3_BL};
static const int SK_KEY_PINS[] = {PIN_SK1_KEY, PIN_SK2_KEY, PIN_SK3_KEY};

#endif // PINS_H
