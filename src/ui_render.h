#ifndef UI_RENDER_H
#define UI_RENDER_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "agent_registry.h"

void drawMicIcon(Arduino_GFX* g);
void drawCheckIcon(Arduino_GFX* g);
void drawSwitchAgentIcon(Arduino_GFX* g, const char* label);
void drawEscIcon(Arduino_GFX* g, const char* label);
void drawBackspaceIcon(Arduino_GFX* g);

void drawRectMetadata(Arduino_GFX& rectLcd, const AgentSlot* slots,
                      uint8_t selectedAgent, bool voiceRecording,
                      bool voiceEditing);
void drawWifiStatusBar(Arduino_GFX& rectLcd, const String& mode,
                       const String& ip, const String& apSsid,
                       const String& apPassword);
void drawExprFrame(Arduino_GFX& roundLcd, AgentState selectedState,
                   bool voiceRecording, uint8_t& currentExpr,
                   uint8_t& currentFrame, bool clear);

void drawPairingRound(Arduino_GFX* g, const char* code);
void drawPairingRect(Arduino_GFX& rectLcd, const char* code);

#endif // UI_RENDER_H
