#include "display_hardware.h"
#include "pins.h"

// Board soldering variant switch:
//   0 = normal PCB wiring
//   1 = physical ScreenKey 1 and ScreenKey 3 are swapped by soldering
#ifndef KIRO_HW_SWAP_KEY1_KEY3
#define KIRO_HW_SWAP_KEY1_KEY3 0
#endif

// Round LCD (J5) - GC9D01 160x160
static Arduino_ESP32SPI busRound(PIN_RLCD_DC, PIN_RLCD_CS, PIN_SPIB_CLK, PIN_SPIB_MOSI, -1, FSPI);
static Arduino_GC9D01 roundLcd(&busRound, PIN_RLCD_RST, 0, false);

// ScreenKey 1/2/3 share HSPI. CS is manually gated so exactly one panel is
// selected for every transaction.
static Arduino_ESP32SPI busSk1(PIN_SK1_DC, GFX_NOT_DEFINED, PIN_SPIA_CLK, PIN_SPIA_MOSI, -1, HSPI);
static Arduino_ST7735 sk1(&busSk1, PIN_SK1_RST, 0, true, 128, 128, 2, 3, 0, 0);

static Arduino_ESP32SPI busSk2(PIN_SK2_DC, GFX_NOT_DEFINED, PIN_SPIA_CLK, PIN_SPIA_MOSI, -1, HSPI);
static Arduino_ST7735 sk2(&busSk2, PIN_SK2_RST, 0, true, 128, 128, 2, 3, 0, 0);

static Arduino_ESP32SPI busSk3(PIN_SK3_DC, GFX_NOT_DEFINED, PIN_SPIA_CLK, PIN_SPIA_MOSI, -1, HSPI);
static Arduino_ST7735 sk3(&busSk3, PIN_SK3_RST, 0, true, 128, 128, 2, 3, 0, 0);

// Rect LCD 1.47" (J6) - ST7789V3 172x320, landscape
static Arduino_ESP32SPI busRect(PIN_RECT_DC, PIN_RECT_CS, PIN_SPIB_CLK, PIN_SPIB_MOSI, -1, FSPI);
static Arduino_ST7789 rectLcd(&busRect, PIN_RECT_RST, 1, true, 172, 320, 34, 0, 34, 0);

static Arduino_GFX* const SCREEN_KEYS[3] = {&sk1, &sk2, &sk3};
static const int8_t SCREEN_KEY_CS[3] = {PIN_SK1_CS, PIN_SK2_CS, PIN_SK3_CS};

Arduino_GFX& roundDisplay() {
  return roundLcd;
}

Arduino_GFX& rectDisplay() {
  return rectLcd;
}

KeyBinding bindingForLogical(LogicalKey key) {
  if (key == KEY_MIDDLE_LOGICAL) {
    return {1, PIN_SK2_KEY};
  }

  // The soldering variant swaps the whole logical key, not just the input pin:
  // display content and button action must move together.
  if (KIRO_HW_SWAP_KEY1_KEY3) {
    return (key == KEY_LEFT_LOGICAL)
      ? KeyBinding{2, PIN_SK3_KEY}
      : KeyBinding{0, PIN_SK1_KEY};
  }

  return (key == KEY_LEFT_LOGICAL)
    ? KeyBinding{0, PIN_SK1_KEY}
    : KeyBinding{2, PIN_SK3_KEY};
}

int pinForLogical(LogicalKey key) {
  return bindingForLogical(key).keyPin;
}

void deselectScreenKeys() {
  for (int k = 0; k < 3; k++) {
    digitalWrite(SCREEN_KEY_CS[k], HIGH);
  }
}

Arduino_GFX* selectLogicalScreen(LogicalKey key) {
  uint8_t physical = bindingForLogical(key).screenIndex;
  for (int k = 0; k < 3; k++) {
    digitalWrite(SCREEN_KEY_CS[k], (k == physical) ? LOW : HIGH);
  }
  return SCREEN_KEYS[physical];
}

static void selectPhysicalScreen(uint8_t physical) {
  for (int k = 0; k < 3; k++) {
    digitalWrite(SCREEN_KEY_CS[k], (k == physical) ? LOW : HIGH);
  }
}

static void initScreenKeys() {
  for (int i = 0; i < 3; i++) {
    pinMode(SK_BL_PINS[i], OUTPUT);
    digitalWrite(SK_BL_PINS[i], HIGH);
    pinMode(SK_KEY_PINS[i], INPUT_PULLUP);
    pinMode(SCREEN_KEY_CS[i], OUTPUT);
  }
  deselectScreenKeys();

  selectPhysicalScreen(0);
  Serial.printf("[LCD] physical sk1 begin: %d\n", sk1.begin());
  deselectScreenKeys();

  selectPhysicalScreen(1);
  Serial.printf("[LCD] physical sk2 begin: %d\n", sk2.begin());
  deselectScreenKeys();

  selectPhysicalScreen(2);
  Serial.printf("[LCD] physical sk3 begin: %d\n", sk3.begin());
  deselectScreenKeys();
}

void beginDisplayHardware() {
  pinMode(PIN_RLCD_BL, OUTPUT);
  digitalWrite(PIN_RLCD_BL, HIGH);
  Serial.printf("[LCD] round begin: %d\n", roundLcd.begin());
  roundLcd.fillScreen(0x0000);

  initScreenKeys();

  pinMode(PIN_RECT_BL, OUTPUT);
  digitalWrite(PIN_RECT_BL, HIGH);
  Serial.printf("[LCD] rect begin: %d\n", rectLcd.begin());
}

void printKeyWiring() {
  KeyBinding leftBinding = bindingForLogical(KEY_LEFT_LOGICAL);
  KeyBinding middleBinding = bindingForLogical(KEY_MIDDLE_LOGICAL);
  KeyBinding rightBinding = bindingForLogical(KEY_RIGHT_LOGICAL);
  Serial.printf("[HW] key wiring: %s\n",
                KIRO_HW_SWAP_KEY1_KEY3 ? "swap-key1-key3" : "normal");
  Serial.printf("[HW] left: screen SK%d + key GPIO%d\n",
                leftBinding.screenIndex + 1,
                leftBinding.keyPin);
  Serial.printf("[HW] middle: screen SK%d + key GPIO%d\n",
                middleBinding.screenIndex + 1,
                middleBinding.keyPin);
  Serial.printf("[HW] right: screen SK%d + key GPIO%d\n",
                rightBinding.screenIndex + 1,
                rightBinding.keyPin);
  Serial.printf("[HW] logical swap couples display and action: %s\n",
                KIRO_HW_SWAP_KEY1_KEY3 ? "enabled" : "disabled");
}
