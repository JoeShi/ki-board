/**
 * Kiro Keyboard - 5-screen agentic coding controller
 *
 * Logical keys:
 *   key_left   -> switch to next Ghostty split (Command + ])
 *   key_middle -> start dictation / send input
 *   key_right  -> ESC / cancel dictation / clear / interrupt
 */

#include <Arduino.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <Arduino_GFX_Library.h>
#include <ArduinoJson.h>
#include <cstring>
#include "pins.h"
#include "kiro_expressions.h"

// Board soldering variant switch:
//   0 = normal PCB wiring
//   1 = physical ScreenKey 1 and ScreenKey 3 are swapped by soldering
#ifndef KIRO_HW_SWAP_KEY1_KEY3
#define KIRO_HW_SWAP_KEY1_KEY3 0
#endif

static constexpr uint8_t AGENT_COUNT = 4;
static constexpr uint16_t DEBOUNCE_MS = 35;
static constexpr uint16_t LONG_PRESS_MS = 700;
static constexpr uint16_t DOUBLE_CLICK_MS = 420;
static constexpr uint16_t HID_TAP_MS = 70;
static constexpr uint16_t FRAME_INTERVAL_MS = 1000 / EXPR_FPS;
static constexpr uint16_t DICTATION_COMMIT_DELAY_MS = 160;

enum AgentState : uint8_t {
  AGENT_IDLE = 0,
  AGENT_RUNNING,
  AGENT_ERROR
};

struct AgentSlot {
  char name[24] = "";
  char sessionId[48] = "";
  char cwd[64] = "";
  AgentState state = AGENT_IDLE;
  bool occupied = false;
  unsigned long lastUpdateMs = 0;
};

USBHIDKeyboard Keyboard;

// Round LCD (J5) - GC9D01 160x160
Arduino_ESP32SPI busRound(PIN_RLCD_DC, PIN_RLCD_CS, PIN_SPIB_CLK, PIN_SPIB_MOSI, -1, FSPI);
Arduino_GC9D01 roundLcd(&busRound, PIN_RLCD_RST, 0, false);

// ScreenKey 1/2/3 share HSPI. CS is manually gated so exactly one panel is
// selected for every transaction.
Arduino_ESP32SPI busSk1(PIN_SK1_DC, GFX_NOT_DEFINED, PIN_SPIA_CLK, PIN_SPIA_MOSI, -1, HSPI);
Arduino_ST7735 sk1(&busSk1, PIN_SK1_RST, 0, true, 128, 128, 2, 3, 0, 0);

Arduino_ESP32SPI busSk2(PIN_SK2_DC, GFX_NOT_DEFINED, PIN_SPIA_CLK, PIN_SPIA_MOSI, -1, HSPI);
Arduino_ST7735 sk2(&busSk2, PIN_SK2_RST, 0, true, 128, 128, 2, 3, 0, 0);

Arduino_ESP32SPI busSk3(PIN_SK3_DC, GFX_NOT_DEFINED, PIN_SPIA_CLK, PIN_SPIA_MOSI, -1, HSPI);
Arduino_ST7735 sk3(&busSk3, PIN_SK3_RST, 0, true, 128, 128, 2, 3, 0, 0);

// Rect LCD 1.47" (J6) - ST7789V3 172x320, landscape
Arduino_ESP32SPI busRect(PIN_RECT_DC, PIN_RECT_CS, PIN_SPIB_CLK, PIN_SPIB_MOSI, -1, FSPI);
Arduino_ST7789 rectLcd(&busRect, PIN_RECT_RST, 1, true, 172, 320, 34, 0, 34, 0);

enum LogicalKey : uint8_t {
  KEY_LEFT_LOGICAL = 0,
  KEY_MIDDLE_LOGICAL = 1,
  KEY_RIGHT_LOGICAL = 2,
  LOGICAL_KEY_COUNT = 3
};

struct ButtonState {
  bool lastRaw = HIGH;
  bool stable = HIGH;
  unsigned long lastChangeMs = 0;
  unsigned long pressedMs = 0;
  unsigned long lastShortReleaseMs = 0;
};

struct KeyBinding {
  uint8_t screenIndex;
  int keyPin;
};

static Arduino_GFX* const SCREEN_KEYS[3] = {&sk1, &sk2, &sk3};
static const int8_t SCREEN_KEY_CS[3] = {PIN_SK1_CS, PIN_SK2_CS, PIN_SK3_CS};

static ButtonState buttons[LOGICAL_KEY_COUNT];
static AgentSlot agentSlots[AGENT_COUNT];

static uint8_t selectedAgent = 0;
static bool voiceRecording = false;
static bool voiceEditing = false;
static AgentState stateBeforeVoice = AGENT_IDLE;
static uint8_t currentExpr = 0;
static uint8_t currentFrame = 0;
static unsigned long lastFrameMs = 0;

static void refreshUi();
static const char* agentDisplayName(uint8_t agentIndex);
static AgentState agentStateAt(uint8_t agentIndex);

static KeyBinding bindingForLogical(LogicalKey key) {
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

static int pinForLogical(LogicalKey key) {
  return bindingForLogical(key).keyPin;
}

static void skDeselectAll() {
  for (int k = 0; k < 3; k++) {
    digitalWrite(SCREEN_KEY_CS[k], HIGH);
  }
}

static Arduino_GFX* selectLogicalScreen(LogicalKey key) {
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

static const char* agentStateName(AgentState state) {
  switch (state) {
    case AGENT_IDLE: return "Idle";
    case AGENT_RUNNING: return "Running";
    case AGENT_ERROR: return "Error";
  }
  return "Unknown";
}

static uint8_t exprForState(AgentState state) {
  if (voiceRecording) return 1;  // wait/listening
  switch (state) {
    case AGENT_RUNNING: return 2;
    case AGENT_ERROR:
      return 1;
    case AGENT_IDLE:
    default:
      return 0;
  }
}

static void hidTap(uint8_t key) {
  Keyboard.press(key);
  delay(HID_TAP_MS);
  Keyboard.releaseAll();
  delay(30);
}

static void sendCommandRightBracket() {
  Keyboard.press(KEY_LEFT_GUI);
  Keyboard.press(']');
  delay(HID_TAP_MS);
  Keyboard.releaseAll();
}

static void sendDoubleControl() {
  hidTap(KEY_LEFT_CTRL);
  delay(90);
  hidTap(KEY_LEFT_CTRL);
}

static void sendClearInput() {
  Keyboard.press(KEY_LEFT_GUI);
  Keyboard.press('a');
  delay(HID_TAP_MS);
  Keyboard.releaseAll();
  delay(40);
  hidTap(KEY_BACKSPACE);
}

static void drawCenteredText(Arduino_GFX* g, const char* text, int y, uint8_t size, uint16_t color) {
  int16_t x1, y1;
  uint16_t w, h;
  g->setTextSize(size);
  g->setTextColor(color);
  g->getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  g->setCursor((g->width() - w) / 2, y);
  g->print(text);
}

static void drawPillLabel(Arduino_GFX* g, const char* label, uint16_t color) {
  g->drawRoundRect(20, 98, 88, 20, 10, color);
  drawCenteredText(g, label, 104, 1, 0xFFFF);
}

static void drawMicIcon(Arduino_GFX* g) {
  g->fillScreen(0x0000);
  const uint16_t cyan = 0x07FF;
  const uint16_t blue = 0x041F;
  g->drawRoundRect(48, 20, 32, 52, 16, cyan);
  g->fillRoundRect(54, 28, 20, 36, 10, blue);
  g->drawRoundRect(36, 46, 56, 34, 18, cyan);
  g->drawFastVLine(64, 80, 12, cyan);
  g->drawFastHLine(50, 92, 28, cyan);
  g->drawPixel(48, 32, 0xFFFF);
  drawPillLabel(g, "VOICE", cyan);
}

static void drawCheckIcon(Arduino_GFX* g) {
  g->fillScreen(0x0000);
  const uint16_t green = 0x07E0;
  g->drawCircle(64, 58, 38, green);
  g->drawCircle(64, 58, 37, green);
  for (int i = 0; i < 4; i++) {
    g->drawLine(39, 59 + i, 57, 77 + i, green);
    g->drawLine(57, 77 + i, 91, 38 + i, green);
  }
  drawPillLabel(g, "SEND", green);
}

static void drawSwitchAgentIcon(Arduino_GFX* g) {
  g->fillScreen(0x0000);
  const uint16_t yellow = 0xFFE0;
  const uint16_t dim = 0x8410;
  g->drawRoundRect(18, 34, 38, 34, 7, dim);
  g->drawRoundRect(72, 34, 38, 34, 7, yellow);
  g->drawLine(48, 51, 88, 51, yellow);
  g->fillTriangle(88, 43, 88, 59, 104, 51, yellow);
  g->drawLine(82, 75, 42, 75, dim);
  g->fillTriangle(42, 67, 42, 83, 26, 75, dim);
  char label[12];
  snprintf(label, sizeof(label), "NEXT %s", agentDisplayName((selectedAgent + 1) % AGENT_COUNT));
  drawPillLabel(g, label, yellow);
}

static void drawEscIcon(Arduino_GFX* g, const char* label) {
  g->fillScreen(0x0000);
  const uint16_t red = 0xF800;
  const uint16_t orange = 0xFD20;
  g->drawRoundRect(26, 32, 76, 52, 9, red);
  g->drawRoundRect(30, 36, 68, 44, 7, orange);
  g->setTextColor(red);
  g->setTextSize(2);
  g->setCursor(45, 51);
  g->print("ESC");
  drawPillLabel(g, label, red);
}

static void drawBackspaceIcon(Arduino_GFX* g) {
  g->fillScreen(0x0000);
  const uint16_t orange = 0xFD20;
  g->drawRoundRect(42, 38, 62, 42, 6, orange);
  g->drawLine(42, 38, 22, 59, orange);
  g->drawLine(22, 59, 42, 80, orange);
  g->drawLine(54, 51, 78, 75, orange);
  g->drawLine(78, 51, 54, 75, orange);
  g->drawLine(23, 59, 102, 59, orange);
  drawPillLabel(g, "BACK", orange);
}

static void drawApproveIcon(Arduino_GFX* g) {
  g->fillScreen(0x0000);
  const uint16_t green = 0x07E0;
  g->drawRoundRect(28, 30, 72, 56, 12, green);
  g->drawFastHLine(42, 45, 44, green);
  g->drawFastHLine(42, 58, 32, green);
  for (int i = 0; i < 3; i++) {
    g->drawLine(50, 70 + i, 61, 80 + i, green);
    g->drawLine(61, 80 + i, 84, 54 + i, green);
  }
  drawPillLabel(g, "APPROVE", green);
}

static void drawRejectIcon(Arduino_GFX* g) {
  g->fillScreen(0x0000);
  const uint16_t red = 0xF800;
  g->drawRoundRect(28, 30, 72, 56, 12, red);
  g->drawFastHLine(42, 45, 44, red);
  g->drawFastHLine(42, 58, 32, red);
  for (int i = 0; i < 4; i++) {
    g->drawLine(46, 70 + i, 82, 34 + i, red);
    g->drawLine(82, 70 + i, 46, 34 + i, red);
  }
  drawPillLabel(g, "REJECT", red);
}

static void drawKey(LogicalKey key) {
  Arduino_GFX* g = selectLogicalScreen(key);
  AgentState state = agentStateAt(selectedAgent);
  const bool voiceActive = voiceRecording || voiceEditing;

  if (key == KEY_LEFT_LOGICAL) {
    drawSwitchAgentIcon(g);
  } else if (key == KEY_MIDDLE_LOGICAL) {
    if (voiceActive) {
      drawCheckIcon(g);
    } else {
      drawMicIcon(g);
    }
  } else {
    if (voiceRecording) {
      drawEscIcon(g, "DICTATE");
    } else if (voiceEditing) {
      drawBackspaceIcon(g);
    } else if (state == AGENT_RUNNING) {
      drawEscIcon(g, "STOP");
    } else if (state == AGENT_ERROR) {
      drawEscIcon(g, "RESET");
    } else {
      drawEscIcon(g, "CANCEL");
    }
  }
  skDeselectAll();
}

static void drawAllKeys() {
  drawKey(KEY_LEFT_LOGICAL);
  drawKey(KEY_MIDDLE_LOGICAL);
  drawKey(KEY_RIGHT_LOGICAL);
}

static const char* compactAgentStateName(uint8_t agentIndex) {
  if (agentIndex == selectedAgent && voiceRecording) return "REC";
  if (agentIndex == selectedAgent && voiceEditing) return "EDIT";
  if (!agentSlots[agentIndex].occupied) return "Empty";

  switch (agentSlots[agentIndex].occupied ? agentSlots[agentIndex].state : AGENT_IDLE) {
    case AGENT_IDLE: return "Idle";
    case AGENT_RUNNING: return "Run";
    case AGENT_ERROR: return "Error";
  }
  return "Unknown";
}

static const char* agentDisplayName(uint8_t agentIndex) {
  if (!agentSlots[agentIndex].occupied || agentSlots[agentIndex].name[0] == '\0') {
    return "EMPTY";
  }
  return agentSlots[agentIndex].name;
}

static AgentState agentStateAt(uint8_t agentIndex) {
  return agentSlots[agentIndex].occupied ? agentSlots[agentIndex].state : AGENT_IDLE;
}

static void drawAgentName(Arduino_GFX* g, const char* name, int centerX, int y, uint16_t color) {
  size_t len = strlen(name);
  uint8_t size = (len <= 8) ? 2 : 1;
  int16_t x1, y1;
  uint16_t w, h;
  g->setTextSize(size);
  g->setTextColor(color);
  g->getTextBounds(name, 0, y, &x1, &y1, &w, &h);
  g->setCursor(centerX - (int)w / 2, y);
  g->print(name);
}

static uint8_t findAgentSlotByName(const char* name) {
  for (uint8_t i = 0; i < AGENT_COUNT; i++) {
    if (agentSlots[i].occupied && strcmp(agentSlots[i].name, name) == 0) {
      return i;
    }
  }
  return AGENT_COUNT;
}

static uint8_t findAgentSlotBySessionId(const char* sid) {
  if (!sid || !sid[0]) return AGENT_COUNT;
  for (uint8_t i = 0; i < AGENT_COUNT; i++) {
    if (agentSlots[i].occupied && strcmp(agentSlots[i].sessionId, sid) == 0) {
      return i;
    }
  }
  return AGENT_COUNT;
}

static uint8_t chooseAgentSlot(const char* name, const char* sessionId) {
  // Priority: match by session_id first, then by name, then find empty/oldest
  uint8_t bySession = findAgentSlotBySessionId(sessionId);
  if (bySession < AGENT_COUNT) return bySession;

  uint8_t byName = findAgentSlotByName(name);
  if (byName < AGENT_COUNT) return byName;

  for (uint8_t i = 0; i < AGENT_COUNT; i++) {
    if (!agentSlots[i].occupied) return i;
  }

  uint8_t oldest = 0;
  unsigned long oldestUpdate = agentSlots[0].lastUpdateMs;
  for (uint8_t i = 1; i < AGENT_COUNT; i++) {
    if (agentSlots[i].lastUpdateMs < oldestUpdate) {
      oldest = i;
      oldestUpdate = agentSlots[i].lastUpdateMs;
    }
  }
  return oldest;
}

static void applyRegistryEvent(const char* agentName, AgentState state, const char* sessionId, const char* cwd) {
  if (!agentName || !agentName[0]) {
    return;
  }
  uint8_t slot = chooseAgentSlot(agentName, sessionId);
  AgentSlot& agent = agentSlots[slot];
  strncpy(agent.name, agentName, sizeof(agent.name) - 1);
  agent.name[sizeof(agent.name) - 1] = '\0';
  if (sessionId && sessionId[0]) {
    strncpy(agent.sessionId, sessionId, sizeof(agent.sessionId) - 1);
    agent.sessionId[sizeof(agent.sessionId) - 1] = '\0';
  }
  if (cwd && cwd[0]) {
    strncpy(agent.cwd, cwd, sizeof(agent.cwd) - 1);
    agent.cwd[sizeof(agent.cwd) - 1] = '\0';
  }
  agent.state = state;
  agent.occupied = true;
  agent.lastUpdateMs = millis();
  if (selectedAgent >= AGENT_COUNT) {
    selectedAgent = 0;
  }
  Serial.printf("[REG] %s -> %s\n", agent.name, agentStateName(state));
  refreshUi();
}

static void pollRegistrySerial() {
  static char line[256];
  static size_t len = 0;
  while (Serial.available() > 0) {
    int ch = Serial.read();
    if (ch < 0) {
      break;
    }
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      line[len] = '\0';
      if (len > 0 && line[0] == '{') {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, line);
        if (!err) {
          const char* type = doc["type"] | "";
          if (strcmp(type, "agent_state") == 0) {
            const char* agentName = doc["agent_name"] | "";
            const char* stateText = doc["state"] | "";
            const char* sessionId = doc["session_id"] | "";
            const char* cwd = doc["cwd"] | "";
            AgentState state = AGENT_IDLE;
            if (strcmp(stateText, "running") == 0) {
              state = AGENT_RUNNING;
            } else if (strcmp(stateText, "error") == 0) {
              state = AGENT_ERROR;
            }
            applyRegistryEvent(agentName, state, sessionId, cwd);
          }
        }
      }
      len = 0;
      continue;
    }
    if (len + 1 < sizeof(line)) {
      line[len++] = static_cast<char>(ch);
    } else {
      len = 0;
    }
  }
}

static void drawMiniWave(int x, int y, int w, uint16_t color) {
  int mid = y + 10;
  for (int px = x; px < x + w - 8; px += 10) {
    int y2 = mid + ((px / 10) % 2 == 0 ? -6 : 6);
    rectLcd.drawLine(px, mid, px + 8, y2, color);
  }
}

static void drawAgentTile(uint8_t agentIndex, int x, int y, int w, int h) {
  const bool selected = agentIndex == selectedAgent;
  const uint16_t border = selected ? 0x07FF : 0x4208;
  const uint16_t title = selected ? 0x07FF : 0xFFFF;
  const uint16_t stateColor = !agentSlots[agentIndex].occupied ? 0x4208 :
                              agentStateAt(agentIndex) == AGENT_RUNNING ? 0x07E0 :
                              agentStateAt(agentIndex) == AGENT_ERROR ? 0xF800 :
                              0xC618;
  const uint16_t muted = 0x8410;

  rectLcd.drawRoundRect(x, y, w, h, 6, border);
  if (selected) {
    rectLcd.drawRoundRect(x + 2, y + 2, w - 4, h - 4, 5, border);
    rectLcd.setTextColor(border);
    rectLcd.setTextSize(1);
    rectLcd.setCursor(x + 7, y + 7);
    rectLcd.print(">");
  }

  drawAgentName(&rectLcd, agentDisplayName(agentIndex), x + w / 2, y + 10, title);

  rectLcd.setTextSize(1);
  rectLcd.setTextColor(stateColor);
  rectLcd.setCursor(x + 10, y + 40);
  rectLcd.print(compactAgentStateName(agentIndex));

  rectLcd.setTextColor(muted);
  rectLcd.setCursor(x + 10, y + h - 18);
  if (agentSlots[agentIndex].cwd[0]) {
    // Show last path component; truncate with "..." if too long
    const char* full = agentSlots[agentIndex].cwd;
    const char* base = strrchr(full, '/');
    const char* display = base ? base + 1 : full;
    size_t dlen = strlen(display);
    const size_t maxChars = (w - 20) / 6; // ~6px per char at size 1
    if (dlen <= maxChars) {
      rectLcd.print(display);
    } else {
      char buf[32];
      size_t show = (maxChars > 3) ? maxChars - 3 : 0;
      memcpy(buf, display, show);
      buf[show] = '\0';
      rectLcd.print(buf);
      rectLcd.print("...");
    }
  } else {
    rectLcd.print("--");
  }

  if (selected && voiceRecording) {
    drawMiniWave(x + 58, y + 38, w - 68, border);
  }
}

static void drawRectMetadata() {
  const uint16_t BG = 0x0000;
  rectLcd.fillScreen(BG);

  const int margin = 6;
  const int gap = 6;
  const int tileW = (rectLcd.width() - margin * 2 - gap) / 2;
  const int tileH = (rectLcd.height() - margin * 2 - gap) / 2;
  const int leftX = margin;
  const int rightX = margin + tileW + gap;
  const int topY = margin;
  const int bottomY = margin + tileH + gap;

  // Ghostty split order: left-top, left-bottom, right-top, right-bottom.
  drawAgentTile(0, leftX, topY, tileW, tileH);
  drawAgentTile(1, leftX, bottomY, tileW, tileH);
  drawAgentTile(2, rightX, topY, tileW, tileH);
  drawAgentTile(3, rightX, bottomY, tileW, tileH);
}

static void drawExprFrame(bool clear = false) {
  uint8_t desiredExpr = exprForState(agentStateAt(selectedAgent));
  if (desiredExpr != currentExpr) {
    currentExpr = desiredExpr;
    currentFrame = 0;
    clear = true;
  }
  if (clear) {
    roundLcd.fillScreen(0x0000);
  }
  const uint16_t* frame = kiro_expressions[currentExpr][currentFrame];
  const int x = (160 - EXPR_FRAME_W) / 2;
  const int y = (160 - EXPR_FRAME_H) / 2;
  roundLcd.draw16bitRGBBitmap(x, y, frame, EXPR_FRAME_W, EXPR_FRAME_H);
}

static void refreshUi() {
  drawAllKeys();
  drawRectMetadata();
  drawExprFrame(true);
}

static void switchAgent() {
  selectedAgent = (selectedAgent + 1) % AGENT_COUNT;
  voiceRecording = false;
  voiceEditing = false;
  sendCommandRightBracket();
  Serial.printf("[AGENT] selected Agent %d\n", selectedAgent + 1);
  refreshUi();
}

static void startVoiceInput() {
  stateBeforeVoice = agentStateAt(selectedAgent);
  voiceRecording = true;
  voiceEditing = false;
  sendDoubleControl();
  Serial.printf("[VOICE] Agent %d recording\n", selectedAgent + 1);
  refreshUi();
}

static void sendVoiceInput() {
  hidTap(KEY_RETURN);
  voiceRecording = false;
  voiceEditing = false;
  agentSlots[selectedAgent].state = AGENT_RUNNING;
  agentSlots[selectedAgent].occupied = true;
  agentSlots[selectedAgent].lastUpdateMs = millis();
  Serial.printf("[VOICE] Agent %d sent input\n", selectedAgent + 1);
  refreshUi();
}

static void stopDictationForEditing() {
  hidTap(KEY_ESC);
  voiceRecording = false;
  voiceEditing = true;
  Serial.printf("[VOICE] Agent %d stopped dictation, editing input\n", selectedAgent + 1);
  refreshUi();
}

static void clearVoiceInput() {
  if (voiceRecording) {
    hidTap(KEY_ESC);
    delay(DICTATION_COMMIT_DELAY_MS);
  }
  voiceRecording = false;
  voiceEditing = false;
  sendClearInput();
  Serial.printf("[VOICE] Agent %d cleared input\n", selectedAgent + 1);
  refreshUi();
}

static void handleMiddleShort() {
  if (voiceRecording || voiceEditing) {
    sendVoiceInput();
  } else {
    startVoiceInput();
  }
}

static void handleRightShort(bool doubleClick) {
  AgentState state = agentStateAt(selectedAgent);
  if (voiceRecording) {
    stopDictationForEditing();
    return;
  }

  if (voiceEditing) {
    if (doubleClick) {
      clearVoiceInput();
      return;
    }
    hidTap(KEY_BACKSPACE);
    Serial.println("[VOICE] Backspace");
    return;
  }

  hidTap(KEY_ESC);
  if (state == AGENT_RUNNING) {
    agentSlots[selectedAgent].state = AGENT_IDLE;
    agentSlots[selectedAgent].occupied = true;
    agentSlots[selectedAgent].lastUpdateMs = millis();
  }
  Serial.printf("[KEY] ESC for Agent %d\n", selectedAgent + 1);
  refreshUi();
}

static void handleRightLong() {
  if (voiceRecording || voiceEditing) {
    clearVoiceInput();
    return;
  }

  handleRightShort(false);
}

static constexpr uint16_t AGENT_REMOVE_HOLD_MS = 5000;

static void removeCurrentAgentSlot() {
  AgentSlot& agent = agentSlots[selectedAgent];
  agent.name[0] = '\0';
  agent.sessionId[0] = '\0';
  agent.cwd[0] = '\0';
  agent.state = AGENT_IDLE;
  agent.occupied = false;
  agent.lastUpdateMs = 0;
  Serial.printf("[AGENT] Removed slot %d\n", selectedAgent + 1);
  refreshUi();
}

static void handleButtonRelease(LogicalKey key, unsigned long heldMs) {
  const bool longPress = heldMs >= LONG_PRESS_MS;
  if (key == KEY_LEFT_LOGICAL) {
    if (heldMs >= AGENT_REMOVE_HOLD_MS) {
      removeCurrentAgentSlot();
    } else {
      switchAgent();
    }
  } else if (key == KEY_MIDDLE_LOGICAL) {
    handleMiddleShort();
  } else if (longPress) {
    handleRightLong();
  } else {
    unsigned long now = millis();
    const bool wasVoiceEditing = voiceEditing;
    bool doubleClick = wasVoiceEditing && (now - buttons[KEY_RIGHT_LOGICAL].lastShortReleaseMs <= DOUBLE_CLICK_MS);
    buttons[KEY_RIGHT_LOGICAL].lastShortReleaseMs = wasVoiceEditing ? now : 0;
    handleRightShort(doubleClick);
  }
}

static void pollButtons() {
  unsigned long now = millis();
  for (uint8_t i = 0; i < LOGICAL_KEY_COUNT; i++) {
    LogicalKey key = static_cast<LogicalKey>(i);
    bool raw = digitalRead(pinForLogical(key));
    ButtonState& btn = buttons[i];

    if (raw != btn.lastRaw) {
      btn.lastChangeMs = now;
      btn.lastRaw = raw;
    }

    if ((now - btn.lastChangeMs) > DEBOUNCE_MS && raw != btn.stable) {
      btn.stable = raw;
      if (btn.stable == LOW) {
        btn.pressedMs = now;
      } else {
        handleButtonRelease(key, now - btn.pressedMs);
      }
    }
  }
}

static void initScreenKeys() {
  for (int i = 0; i < 3; i++) {
    pinMode(SK_BL_PINS[i], OUTPUT);
    digitalWrite(SK_BL_PINS[i], HIGH);
    pinMode(SK_KEY_PINS[i], INPUT_PULLUP);
    pinMode(SCREEN_KEY_CS[i], OUTPUT);
  }
  skDeselectAll();

  selectPhysicalScreen(0);
  Serial.printf("[LCD] physical sk1 begin: %d\n", sk1.begin());
  skDeselectAll();

  selectPhysicalScreen(1);
  Serial.printf("[LCD] physical sk2 begin: %d\n", sk2.begin());
  skDeselectAll();

  selectPhysicalScreen(2);
  Serial.printf("[LCD] physical sk3 begin: %d\n", sk3.begin());
  skDeselectAll();
}

void setup() {
  Serial.begin(115200);

  Keyboard.begin();
  USB.begin();

  pinMode(PIN_RLCD_BL, OUTPUT);
  digitalWrite(PIN_RLCD_BL, HIGH);
  Serial.printf("[LCD] round begin: %d\n", roundLcd.begin());
  roundLcd.fillScreen(0x0000);

  initScreenKeys();

  pinMode(PIN_RECT_BL, OUTPUT);
  digitalWrite(PIN_RECT_BL, HIGH);
  Serial.printf("[LCD] rect begin: %d\n", rectLcd.begin());

  for (uint8_t i = 0; i < LOGICAL_KEY_COUNT; i++) {
    buttons[i].lastRaw = digitalRead(pinForLogical(static_cast<LogicalKey>(i)));
    buttons[i].stable = buttons[i].lastRaw;
  }

  lastFrameMs = millis();
  refreshUi();

  Serial.println("Kiro KB ready (5-screen agent controller)");
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

void loop() {
  unsigned long now = millis();

  if (now - lastFrameMs >= FRAME_INTERVAL_MS) {
    lastFrameMs = now;
    currentFrame = (currentFrame + 1) % EXPR_FRAME_COUNT;
    drawExprFrame();
  }

  pollRegistrySerial();
  pollButtons();
}
