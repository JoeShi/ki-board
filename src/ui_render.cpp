#include "ui_render.h"
#include "kiro_expressions.h"
#include <cstring>

static uint8_t exprForState(AgentState state, bool voiceRecording) {
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

void drawMicIcon(Arduino_GFX* g) {
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

void drawCheckIcon(Arduino_GFX* g) {
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

void drawSwitchAgentIcon(Arduino_GFX* g, const char* label) {
  g->fillScreen(0x0000);
  const uint16_t yellow = 0xFFE0;
  const uint16_t dim = 0x8410;
  g->drawRoundRect(18, 34, 38, 34, 7, dim);
  g->drawRoundRect(72, 34, 38, 34, 7, yellow);
  g->drawLine(48, 51, 88, 51, yellow);
  g->fillTriangle(88, 43, 88, 59, 104, 51, yellow);
  g->drawLine(82, 75, 42, 75, dim);
  g->fillTriangle(42, 67, 42, 83, 26, 75, dim);
  drawPillLabel(g, label, yellow);
}

void drawEscIcon(Arduino_GFX* g, const char* label) {
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

void drawBackspaceIcon(Arduino_GFX* g) {
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

static const char* compactAgentStateName(const AgentSlot* slots, uint8_t agentIndex,
                                         uint8_t selectedAgent, bool voiceRecording,
                                         bool voiceEditing) {
  if (agentIndex == selectedAgent && voiceRecording) return "REC";
  if (agentIndex == selectedAgent && voiceEditing) return "EDIT";
  if (!slots[agentIndex].occupied) return "Empty";

  switch (agentStateAt(slots, agentIndex)) {
    case AGENT_IDLE: return "Idle";
    case AGENT_RUNNING: return "Run";
    case AGENT_ERROR: return "Error";
  }
  return "Unknown";
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

static void drawMiniWave(Arduino_GFX& rectLcd, int x, int y, int w, uint16_t color) {
  int mid = y + 10;
  for (int px = x; px < x + w - 8; px += 10) {
    int y2 = mid + ((px / 10) % 2 == 0 ? -6 : 6);
    rectLcd.drawLine(px, mid, px + 8, y2, color);
  }
}

static void drawAgentTile(Arduino_GFX& rectLcd, const AgentSlot* slots,
                          uint8_t selectedAgent, bool voiceRecording,
                          bool voiceEditing, uint8_t agentIndex,
                          int x, int y, int w, int h) {
  const bool selected = agentIndex == selectedAgent;
  const uint16_t border = selected ? 0x07FF : 0x4208;
  const uint16_t title = selected ? 0x07FF : 0xFFFF;
  const uint16_t stateColor = !slots[agentIndex].occupied ? 0x4208 :
                              agentStateAt(slots, agentIndex) == AGENT_RUNNING ? 0x07E0 :
                              agentStateAt(slots, agentIndex) == AGENT_ERROR ? 0xF800 :
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

  drawAgentName(&rectLcd, agentDisplayName(slots, agentIndex), x + w / 2, y + 10, title);

  rectLcd.setTextSize(1);
  rectLcd.setTextColor(stateColor);
  rectLcd.setCursor(x + 10, y + 40);
  rectLcd.print(compactAgentStateName(slots, agentIndex, selectedAgent, voiceRecording, voiceEditing));

  rectLcd.setTextColor(muted);
  rectLcd.setCursor(x + 10, y + h - 18);
  if (slots[agentIndex].cwd[0]) {
    const char* full = slots[agentIndex].cwd;
    const char* base = strrchr(full, '/');
    const char* display = base ? base + 1 : full;
    size_t dlen = strlen(display);
    const size_t maxChars = (w - 20) / 6;
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
    drawMiniWave(rectLcd, x + 58, y + 38, w - 68, border);
  }
}

void drawRectMetadata(Arduino_GFX& rectLcd, const AgentSlot* slots,
                      uint8_t selectedAgent, bool voiceRecording,
                      bool voiceEditing) {
  rectLcd.fillScreen(0x0000);

  const int margin = 6;
  const int gap = 6;
  const int tileW = (rectLcd.width() - margin * 2 - gap) / 2;
  const int tileH = (rectLcd.height() - margin * 2 - gap) / 2;
  const int leftX = margin;
  const int rightX = margin + tileW + gap;
  const int topY = margin;
  const int bottomY = margin + tileH + gap;

  drawAgentTile(rectLcd, slots, selectedAgent, voiceRecording, voiceEditing, 0, leftX, topY, tileW, tileH);
  drawAgentTile(rectLcd, slots, selectedAgent, voiceRecording, voiceEditing, 1, leftX, bottomY, tileW, tileH);
  drawAgentTile(rectLcd, slots, selectedAgent, voiceRecording, voiceEditing, 2, rightX, topY, tileW, tileH);
  drawAgentTile(rectLcd, slots, selectedAgent, voiceRecording, voiceEditing, 3, rightX, bottomY, tileW, tileH);
}

void drawWifiStatusBar(Arduino_GFX& rectLcd, const String& mode,
                       const String& ip, const String& apSsid,
                       const String& apPassword) {
  const int y = rectLcd.height() - 18;
  rectLcd.fillRect(0, y, rectLcd.width(), 18, 0x0000);
  rectLcd.drawFastHLine(0, y, rectLcd.width(), 0x2945);
  rectLcd.setTextSize(1);
  rectLcd.setTextColor(mode == "AP" ? 0xFFE0 : 0x07FF);
  rectLcd.setCursor(6, y + 5);

  if (mode == "AP") {
    rectLcd.print(apSsid);
    rectLcd.print(" ");
    rectLcd.print(apPassword);
    rectLcd.print(" 192.168.4.1");
    return;
  }

  rectLcd.print("WiFi ");
  rectLcd.print(mode);
  if (ip.length() > 0) {
    rectLcd.print(" ");
    rectLcd.print(ip);
  }
  if (mode == "STA") {
    rectLcd.print(" kirokb.local");
  }
}

void drawExprFrame(Arduino_GFX& roundLcd, AgentState selectedState,
                   bool voiceRecording, uint8_t& currentExpr,
                   uint8_t& currentFrame, bool clear) {
  uint8_t desiredExpr = exprForState(selectedState, voiceRecording);
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

void drawPairingRound(Arduino_GFX* g, const char* code) {
  const uint16_t cyan = 0x07FF;
  g->fillScreen(0x0000);
  drawCenteredText(g, "PAIR", 30, 2, cyan);
  drawCenteredText(g, code, 78, 3, 0xFFFF);
  drawCenteredText(g, "check on Mac", 118, 1, 0x8410);
}

void drawPairingRect(Arduino_GFX& rectLcd, const char* code) {
  const uint16_t cyan = 0x07FF;
  const uint16_t green = 0x07E0;
  const uint16_t red = 0xF800;
  rectLcd.fillScreen(0x0000);
  drawCenteredText(&rectLcd, "Pair with Mac?", 14, 2, cyan);
  drawCenteredText(&rectLcd, code, 44, 4, 0xFFFF);
  rectLcd.setTextSize(2);
  rectLcd.setTextColor(green);
  rectLcd.setCursor(10, rectLcd.height() - 28);
  rectLcd.print("OK confirm");
  rectLcd.setTextColor(red);
  rectLcd.setCursor(10, rectLcd.height() - 54);
  rectLcd.print("ESC cancel");
}
