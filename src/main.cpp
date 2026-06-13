/**
 * Kiro Keyboard - USB HID with Voice Input Toggle + Animated Expressions
 *
 * Key1: Toggle between mic (double-tap Control) and check (Enter+ESC)
 * Round LCD: 3 animated expressions cycling every 10 seconds
 */

#include <Arduino.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <Arduino_GFX_Library.h>
#include "kiro_expressions.h"

// USB HID Keyboard
USBHIDKeyboard Keyboard;

// Round LCD (J5) - GC9D01 160x160
#define RLCD_MOSI  12
#define RLCD_CLK   11
#define RLCD_CS    16
#define RLCD_DC    6
#define RLCD_RST   40
#define RLCD_BL    47

// ScreenKey 1 (J1)
#define SK1_MOSI   9
#define SK1_CLK    10
#define SK1_CS     13
#define SK1_DC     18
#define SK1_RST    4
#define SK1_BL     42
#define SK1_KEY    1

Arduino_ESP32SPI busB(RLCD_DC, RLCD_CS, RLCD_CLK, RLCD_MOSI, -1);
Arduino_GC9D01  roundLcd(&busB, RLCD_RST, 0, false);

Arduino_ESP32SPI busA(SK1_DC, SK1_CS, SK1_CLK, SK1_MOSI, -1);
Arduino_ST7735   sk1(&busA, SK1_RST, 0, true, 128, 128, 2, 3, 0, 0);

// Expression animation state
static uint8_t currentExpr = 0;        // 0=idle, 1=wait, 2=work
static uint8_t currentFrame = 0;
static unsigned long lastFrameMs = 0;
static unsigned long exprStartMs = 0;
#define EXPR_SWITCH_MS 10000            // 10 seconds per expression
#define FRAME_INTERVAL_MS (1000 / EXPR_FPS)  // 250ms per frame at 4fps

// Offset to center 120x120 in 160x160
#define EXPR_X_OFFSET ((160 - EXPR_FRAME_W) / 2)
#define EXPR_Y_OFFSET ((160 - EXPR_FRAME_H) / 2)

// Key1 state
static bool micMode = true;
static bool lastBtn = HIGH;
static unsigned long lastDebounce = 0;

void drawMic() {
  sk1.fillScreen(0x0000);
  uint16_t col = 0x07FF;
  sk1.fillRoundRect(48, 25, 32, 45, 16, col);
  sk1.fillRect(58, 70, 12, 15, col);
  sk1.drawRoundRect(38, 50, 52, 40, 20, col);
  sk1.fillRect(50, 85, 28, 4, col);
  sk1.setTextColor(0xFFFF);
  sk1.setTextSize(1);
  sk1.setCursor(48, 100);
  sk1.print("Voice");
}

void drawCheck() {
  sk1.fillScreen(0x0000);
  uint16_t col = 0x07E0;
  for (int i = 0; i < 4; i++) {
    sk1.drawLine(30, 60+i, 55, 85+i, col);
    sk1.drawLine(55, 85+i, 98, 35+i, col);
  }
  sk1.setTextColor(0xFFFF);
  sk1.setTextSize(1);
  sk1.setCursor(48, 100);
  sk1.print("Send");
}

void drawExprFrame() {
  const uint16_t* frame = kiro_expressions[currentExpr][currentFrame];
  roundLcd.draw16bitRGBBitmap(EXPR_X_OFFSET, EXPR_Y_OFFSET, frame, EXPR_FRAME_W, EXPR_FRAME_H);
}

void setup() {
  Serial.begin(115200);

  // USB HID
  Keyboard.begin();
  USB.begin();

  // Round LCD
  pinMode(RLCD_BL, OUTPUT);
  digitalWrite(RLCD_BL, HIGH);
  roundLcd.begin();
  roundLcd.fillScreen(0x0000);

  // ScreenKey 1
  pinMode(SK1_BL, OUTPUT);
  digitalWrite(SK1_BL, HIGH);
  pinMode(SK1_KEY, INPUT_PULLUP);
  sk1.begin();
  drawMic();

  // Draw first frame
  drawExprFrame();
  lastFrameMs = millis();
  exprStartMs = millis();

  Serial.println("Kiro KB ready (USB HID + expressions)");
}

void loop() {
  unsigned long now = millis();

  // --- Expression animation: advance frame ---
  if (now - lastFrameMs >= FRAME_INTERVAL_MS) {
    lastFrameMs = now;
    currentFrame = (currentFrame + 1) % EXPR_FRAME_COUNT;
    drawExprFrame();
  }

  // --- Switch expression every 10 seconds ---
  if (now - exprStartMs >= EXPR_SWITCH_MS) {
    exprStartMs = now;
    currentExpr = (currentExpr + 1) % EXPR_COUNT;
    currentFrame = 0;
    roundLcd.fillScreen(0x0000);  // clear before new expression
    drawExprFrame();
  }

  // --- Key1 toggle: mic <-> check ---
  bool btn = digitalRead(SK1_KEY);
  if (btn == LOW && lastBtn == HIGH && (now - lastDebounce > 200)) {
    lastDebounce = now;

    if (micMode) {
      // Double-tap Control to invoke voice input
      Keyboard.press(KEY_LEFT_CTRL);
      delay(80);
      Keyboard.releaseAll();
      delay(80);
      Keyboard.press(KEY_LEFT_CTRL);
      delay(80);
      Keyboard.releaseAll();
      Serial.println("Sent: Double Control");
      micMode = false;
      drawCheck();
    } else {
      // Enter to confirm, then ESC to exit dictation
      Keyboard.press(KEY_RETURN);
      delay(80);
      Keyboard.releaseAll();
      delay(200);
      Keyboard.press(KEY_ESC);
      delay(80);
      Keyboard.releaseAll();
      Serial.println("Sent: Enter + ESC");
      micMode = true;
      drawMic();
    }
  }
  lastBtn = btn;
}
