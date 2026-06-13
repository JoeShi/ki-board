/**
 * display.cpp - 4个 ScreenKey 屏幕管理实现
 *
 * 实现要点:
 * - 4个屏幕共享 SPI 总线 (MOSI/SCK) 和 DC/RST, 各自独立 CS
 * - 为每个屏幕创建独立的 Arduino_ESP32SPI 数据总线 (相同DC/SCK/MOSI, 不同CS)
 *   并设置 is_shared_interface=true, 使它们共享底层 SPI 外设
 * - 每个屏幕对应一个 Arduino_ST7735 设备
 *
 * 注意: ST7735 128x128 面板的行列偏移 (col/row offset) 因批次而异,
 *       若显示有错位, 调整 ST7735_COL_OFFSET / ST7735_ROW_OFFSET。
 */

#include "display.h"
#include "pins.h"
#include <Arduino_GFX_Library.h>

// ST7735 128x128 偏移 (实机若错位需微调)
#define ST7735_COL_OFFSET  2
#define ST7735_ROW_OFFSET  3

// 每个屏幕的 CS 引脚
static const int8_t s_csPins[NUM_KEYS] = {
    PIN_LCD_CS0, PIN_LCD_CS1, PIN_LCD_CS2, PIN_LCD_CS3
};

// 每个屏幕的数据总线和显示设备
static Arduino_DataBus* s_bus[NUM_KEYS]   = {nullptr};
static Arduino_GFX*     s_gfx[NUM_KEYS]   = {nullptr};
static bool             s_ready[NUM_KEYS] = {false};

// 当前背光亮度
static uint8_t s_backlightLevel = BL_DEFAULT_LEVEL;

uint8_t displayBegin() {
    uint8_t ok = 0;

    // 背光: 使用 LEDC PWM 调光 (Arduino Core 3.x 新 API)
    // ledcAttach(pin, freq, resolution) 自动分配 LEDC 通道
    ledcAttach(PIN_LCD_BL, BL_PWM_FREQ, BL_PWM_RESOLUTION);
    ledcWrite(PIN_LCD_BL, s_backlightLevel);

    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        // 共享 DC/SCK/MOSI, 独立 CS, 共享 SPI 外设 (FSPI)
        s_bus[i] = new Arduino_ESP32SPI(
            PIN_LCD_DC,        // dc (共享)
            s_csPins[i],       // cs (独立)
            PIN_SPI_CLK,       // sck (共享)
            PIN_SPI_MOSI,      // mosi (共享)
            GFX_NOT_DEFINED,   // miso (不用)
            FSPI,              // ESP32-S3 uses FSPI
            true               // is_shared_interface: 多设备共享总线
        );

        // ST7735, IPS面板, 128x128, 共享 RST
        s_gfx[i] = new Arduino_ST7735(
            s_bus[i],
            PIN_LCD_RST,       // rst (共享)
            0,                 // rotation
            true,              // ips
            LCD_WIDTH, LCD_HEIGHT,
            ST7735_COL_OFFSET, ST7735_ROW_OFFSET,
            ST7735_COL_OFFSET, ST7735_ROW_OFFSET
        );

        if (s_gfx[i]->begin()) {
            s_gfx[i]->fillScreen(COLOR_BLACK);
            s_ready[i] = true;
            ok++;
        } else {
            Serial.printf("[LCD] Screen %d begin() failed\n", i);
            s_ready[i] = false;
        }
    }

    Serial.printf("[LCD] %d/%d screens initialized\n", ok, NUM_KEYS);
    return ok;
}

void displayShowKey(uint8_t index, const char* label, uint16_t color) {
    if (index >= NUM_KEYS || !s_ready[index]) return;

    Arduino_GFX* g = s_gfx[index];
    g->fillScreen(COLOR_BLACK);

    // 居中显示标签 (文字大小2, 每字符约12px宽)
    g->setTextColor(color);
    g->setTextSize(2);
    int16_t textW = strlen(label) * 12;
    int16_t x = (LCD_WIDTH - textW) / 2;
    if (x < 0) x = 0;
    int16_t y = (LCD_HEIGHT - 16) / 2;
    g->setCursor(x, y);
    g->print(label);

    // 底部画一条颜色条作为该键的标识
    g->fillRect(0, LCD_HEIGHT - 8, LCD_WIDTH, 8, color);
}

void displayFlashKey(uint8_t index) {
    if (index >= NUM_KEYS || !s_ready[index]) return;

    // 短暂反色高亮, 表示按下
    Arduino_GFX* g = s_gfx[index];
    g->fillScreen(COLOR_WHITE);
    delay(40);
    // 调用方应在之后重绘正常内容
}

void displayShowEncoderMode(const char* modeLabel) {
    // 在所有屏幕顶部显示当前模式 (简单实现: 第1屏顶部条)
    // 实际布局可在硬件验证后调整
    if (s_ready[0]) {
        Arduino_GFX* g = s_gfx[0];
        g->fillRect(0, 0, LCD_WIDTH, 16, COLOR_BLUE);
        g->setTextColor(COLOR_WHITE);
        g->setTextSize(1);
        g->setCursor(2, 4);
        g->print("Mode:");
        g->print(modeLabel);
    }
}

// === 背光 PWM 调光 ===

void displaySetBacklight(uint8_t level) {
    s_backlightLevel = level;
    ledcWrite(PIN_LCD_BL, level);
    Serial.printf("[LCD] Backlight level: %d\n", level);
}

uint8_t displayGetBacklight() {
    return s_backlightLevel;
}
