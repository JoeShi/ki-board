/**
 * Test: Round LCD - Kiro Ghost Logo with blink animation
 * Black background, white ghost, eyes blink every ~3 seconds
 */

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "kiro_logo.h"

#define RLCD_MOSI  12
#define RLCD_CLK   11
#define RLCD_CS    16
#define RLCD_DC    6
#define RLCD_RST   40
#define RLCD_BL    47

Arduino_ESP32SPI bus(RLCD_DC, RLCD_CS, RLCD_CLK, RLCD_MOSI, -1);
Arduino_GC9D01  gfx(&bus, RLCD_RST, 0, true);

static const int logoX = (160 - LOGO_WIDTH) / 2;
static const int logoY = (160 - LOGO_HEIGHT) / 2;

void setup() {
  Serial.begin(115200);
  pinMode(RLCD_BL, OUTPUT);
  digitalWrite(RLCD_BL, HIGH);

  gfx.begin();
  gfx.fillScreen(0x0000);
  gfx.draw16bitRGBBitmap(logoX, logoY, kiro_idle_normal, LOGO_WIDTH, LOGO_HEIGHT);
  Serial.println("Kiro logo ready - blinking");
}

void loop() {
  // Eyes open for ~3 seconds
  delay(3000);

  // Blink: show closed eyes
  gfx.draw16bitRGBBitmap(logoX, logoY, kiro_idle_blink, LOGO_WIDTH, LOGO_HEIGHT);
  delay(150);

  // Open eyes again
  gfx.draw16bitRGBBitmap(logoX, logoY, kiro_idle_normal, LOGO_WIDTH, LOGO_HEIGHT);
}
