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

Arduino_ESP32SPI busB(RLCD_DC, RLCD_CS, RLCD_CLK, RLCD_MOSI, -1, FSPI);
Arduino_GC9D01  roundLcd(&busB, RLCD_RST, 0, false);

// ScreenKey 1/2/3 share ONE hardware SPI peripheral (HSPI), bus CS is left
// UNMANAGED (GFX_NOT_DEFINED) -> we drive each panel's CS manually so that
// only ONE panel is selected at a time (chip-select gating). This is the
// standard way to share a hardware SPI bus among multiple displays and avoids
// the contention caused by the library toggling several CS lines per transfer.
// DC differs per panel (GPIO18/8/7) so each still needs its own bus object.
Arduino_ESP32SPI busA(SK1_DC, GFX_NOT_DEFINED, SK1_CLK, SK1_MOSI, -1, HSPI);
Arduino_ST7735   sk1(&busA, SK1_RST, 0, true, 128, 128, 2, 3, 0, 0);

// ScreenKey 2 (J11) - shares SPI-A bus (CLK/MOSI), independent CS/DC/RST/BL
#define SK2_CS     14
#define SK2_DC     8
#define SK2_RST    38
#define SK2_BL     43
#define SK2_KEY    2
Arduino_ESP32SPI busA2(SK2_DC, GFX_NOT_DEFINED, SK1_CLK, SK1_MOSI, -1, HSPI);
Arduino_ST7735   sk2(&busA2, SK2_RST, 0, true, 128, 128, 2, 3, 0, 0);

// ScreenKey 3 (J12) - shares SPI-A bus (CLK/MOSI), independent CS/DC/RST/BL
#define SK3_CS     15
#define SK3_DC     7
#define SK3_RST    39
#define SK3_BL     44
#define SK3_KEY    21
Arduino_ESP32SPI busA3(SK3_DC, GFX_NOT_DEFINED, SK1_CLK, SK1_MOSI, -1, HSPI);
Arduino_ST7735   sk3(&busA3, SK3_RST, 0, true, 128, 128, 2, 3, 0, 0);

// Rect LCD 1.47" (J6) - ST7789V3 172x320, shares SPI-B bus (CLK/MOSI)
#define RECT_CS    17
#define RECT_DC    5
#define RECT_RST   41
#define RECT_BL    48
Arduino_ESP32SPI busBrect(RECT_DC, RECT_CS, RLCD_CLK, RLCD_MOSI, -1, FSPI);
// Waveshare 1.47" ST7789: 172x320 panel centered in 240-wide GRAM -> col offset 34
// rotation=1 -> landscape 320x172
Arduino_ST7789   rectLcd(&busBrect, RECT_RST, 1, true, 172, 320, 34, 0, 34, 0);

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

// Manual chip-select gating for the 3 ScreenKeys sharing one HSPI bus.
// Only ONE panel's CS is LOW (selected) at any moment.
static const int8_t SK_CS[3] = {SK1_CS, SK2_CS, SK3_CS};

static void skDeselectAll() {
  for (int k = 0; k < 3; k++) digitalWrite(SK_CS[k], HIGH);
}

// Select exactly one ScreenKey (others deselected)
static void skSelect(int i) {
  for (int k = 0; k < 3; k++) digitalWrite(SK_CS[k], (k == i) ? LOW : HIGH);
}

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

// Draw a thick arc (degrees, y grows downward) for mouth shapes
static void drawArcThick(Arduino_GFX* g, int cx, int cy, int r,
                         int startDeg, int endDeg, uint16_t col, int thick) {
  for (int t = 0; t < thick; t++) {
    for (int a = startDeg; a <= endDeg; a++) {
      float rad = a * 0.01745329252f;  // PI/180
      int x = cx + (int)((r - t) * cosf(rad));
      int y = cy + (int)((r - t) * sinf(rad));
      g->drawPixel(x, y, col);
    }
  }
}

// SK2: smiley face (yellow round face, eyes, upward smile)
void drawSmiley() {
  const uint16_t FACE = 0xFFE0;  // yellow
  const uint16_t DARK = 0x0000;  // black features
  sk2.fillScreen(0x0000);
  sk2.fillCircle(64, 64, 50, FACE);
  sk2.drawCircle(64, 64, 50, DARK);
  sk2.drawCircle(64, 64, 49, DARK);
  // eyes
  sk2.fillCircle(46, 52, 6, DARK);
  sk2.fillCircle(82, 52, 6, DARK);
  // smiling mouth: lower arc of a circle centered above -> U shape
  drawArcThick(&sk2, 64, 58, 28, 25, 155, DARK, 3);
}

// SK3: sad face (blue-ish round face, eyes, downward mouth + tear)
void drawSadFace() {
  const uint16_t FACE = 0xFFE0;  // yellow
  const uint16_t DARK = 0x0000;  // black features
  const uint16_t TEAR = 0x05FF;  // light blue
  sk3.fillScreen(0x0000);
  sk3.fillCircle(64, 64, 50, FACE);
  sk3.drawCircle(64, 64, 50, DARK);
  sk3.drawCircle(64, 64, 49, DARK);
  // eyes
  sk3.fillCircle(46, 52, 6, DARK);
  sk3.fillCircle(82, 52, 6, DARK);
  // sad mouth: upper arc of a circle centered below -> inverted-U shape
  drawArcThick(&sk3, 64, 102, 28, 205, 335, DARK, 3);
  // tear under right eye
  sk3.fillCircle(82, 70, 4, TEAR);
  sk3.fillTriangle(78, 68, 86, 68, 82, 60, TEAR);
}

// Rect LCD: rain icon (cloud + slanted rain lines)
void drawRain() {
  const uint16_t BG    = 0x0000;
  const uint16_t CLOUD = 0xC618;  // light gray
  const uint16_t RAIN  = 0x05FF;  // light blue
  int W = rectLcd.width();
  int H = rectLcd.height();
  rectLcd.fillScreen(BG);
  // cloud: overlapping circles + rounded base, centered horizontally, upper area
  int cx = W / 2;
  int cy = H * 2 / 5;             // cloud center in upper area
  rectLcd.fillCircle(cx - 34, cy, 26, CLOUD);
  rectLcd.fillCircle(cx + 30, cy, 30, CLOUD);
  rectLcd.fillCircle(cx - 4, cy - 22, 34, CLOUD);
  rectLcd.fillRoundRect(cx - 58, cy, 116, 28, 14, CLOUD);
  // rain: slanted lines below the cloud
  int rainTop = cy + 40;
  for (int i = 0; i < 5; i++) {
    int rx = cx - 50 + i * 25;
    for (int d = 0; d < 3; d++) {  // thicker drops
      rectLcd.drawLine(rx + d, rainTop, rx + d - 14, rainTop + 26, RAIN);
      rectLcd.drawLine(rx + d, rainTop + 32, rx + d - 14, rainTop + 58, RAIN);
    }
  }
}

void setup() {
  Serial.begin(115200);

  // USB HID
  Keyboard.begin();
  USB.begin();

  // Round LCD
  pinMode(RLCD_BL, OUTPUT);
  digitalWrite(RLCD_BL, HIGH);
  Serial.printf("[LCD] round begin: %d\n", roundLcd.begin());
  roundLcd.fillScreen(0x0000);

  // ScreenKey 1/2/3 share HSPI with MANUAL chip-select gating.
  // Drive all CS pins HIGH first, then init+draw each panel while ONLY that
  // panel's CS is LOW. This guarantees data/commands reach exactly one panel,
  // so a later begin()/reset can never corrupt an already-drawn panel.
  pinMode(SK1_BL, OUTPUT); digitalWrite(SK1_BL, HIGH);
  pinMode(SK2_BL, OUTPUT); digitalWrite(SK2_BL, HIGH);
  pinMode(SK3_BL, OUTPUT); digitalWrite(SK3_BL, HIGH);
  pinMode(SK1_KEY, INPUT_PULLUP);
  pinMode(SK2_KEY, INPUT_PULLUP);
  pinMode(SK3_KEY, INPUT_PULLUP);
  pinMode(SK1_CS, OUTPUT);
  pinMode(SK2_CS, OUTPUT);
  pinMode(SK3_CS, OUTPUT);
  skDeselectAll();

  skSelect(0);
  Serial.printf("[LCD] sk1 begin: %d\n", sk1.begin());
  drawMic();
  skDeselectAll();

  skSelect(1);
  Serial.printf("[LCD] sk2 begin: %d\n", sk2.begin());
  drawSmiley();
  skDeselectAll();

  skSelect(2);
  Serial.printf("[LCD] sk3 begin: %d\n", sk3.begin());
  drawSadFace();
  skDeselectAll();

  // Rect LCD -> rain icon
  pinMode(RECT_BL, OUTPUT);
  digitalWrite(RECT_BL, HIGH);
  Serial.printf("[LCD] rect begin: %d\n", rectLcd.begin());
  drawRain();

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
      skSelect(0);
      drawCheck();
      skDeselectAll();
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
      skSelect(0);
      drawMic();
      skDeselectAll();
    }
  }
  lastBtn = btn;
}
