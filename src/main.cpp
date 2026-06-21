/**
 * Kiro Keyboard - 5-screen agentic coding controller
 *
 * Logical keys:
 *   key_left   -> ESC / stop dictation / exit edit / interrupt
 *   key_middle -> start dictation / send input
 *   key_right  -> switch agent / backspace in voice modes
 */

#include <Arduino.h>
#include "kiro_expressions.h"
#include "agent_registry.h"
#include "ble_gatt_comm.h"
#include "ble_hid.h"
#include "display_hardware.h"
#include "hid_actions.h"
#include "ota_manager.h"
#include "pairing.h"
#include "ui_render.h"
#include "wifi_config.h"

static constexpr uint16_t DEBOUNCE_MS = 35;
static constexpr uint16_t LONG_PRESS_MS = 700;
static constexpr uint16_t BACKSPACE_REPEAT_MS = 120;
static constexpr uint16_t FRAME_INTERVAL_MS = 1000 / EXPR_FPS;
static constexpr uint16_t DICTATION_COMMIT_DELAY_MS = 160;
static constexpr uint16_t WIFI_STATUS_INTERVAL_MS = 1000;
static constexpr uint16_t WIFI_RESET_HOLD_MS = 5000;
static constexpr uint16_t BLE_STATUS_INTERVAL_MS = 2000;
static constexpr uint16_t PAIRING_ENTER_HOLD_MS = 3000;

struct ButtonState {
  bool lastRaw = HIGH;
  bool stable = HIGH;
  unsigned long lastChangeMs = 0;
  unsigned long pressedMs = 0;
  unsigned long lastRepeatMs = 0;
  bool repeatFired = false;
};

static ButtonState buttons[LOGICAL_KEY_COUNT];
static AgentSlot agentSlots[AGENT_COUNT];

static uint8_t selectedAgent = 0;
static bool voiceRecording = false;
static bool voiceEditing = false;
static uint8_t currentExpr = 0;
static uint8_t currentFrame = 0;
static unsigned long lastFrameMs = 0;
static unsigned long lastWifiStatusMs = 0;
static unsigned long lastBleStatusMs = 0;
static unsigned long lastOtaUiMs = 0;
static uint8_t lastOtaProgress = 255;
static bool wifiResetSuppressReleases = false;
static bool pairingGestureSuppress = false;

static void refreshUi();
static AgentState agentStateAt(uint8_t agentIndex);

static const char* logicalKeyName(LogicalKey key) {
  switch (key) {
    case KEY_LEFT_LOGICAL: return "left";
    case KEY_MIDDLE_LOGICAL: return "middle";
    case KEY_RIGHT_LOGICAL: return "right";
    default: return "unknown";
  }
}

static const char* pressActionName(unsigned long heldMs) {
  return heldMs >= LONG_PRESS_MS ? "long" : "short";
}

static void emitButtonEventWithAction(LogicalKey key, const char* action, unsigned long heldMs);

static const char* voiceIntentName(LogicalKey key) {
  if (key == KEY_MIDDLE_LOGICAL) {
    if (voiceEditing) return "voice_send_edit";
    if (voiceRecording) return "voice_commit_send";
    return "voice_start";
  }
  if (key == KEY_LEFT_LOGICAL) {
    if (voiceRecording) return "voice_commit_edit";
    if (voiceEditing) return "voice_exit_edit";
    return "";
  }
  if (key == KEY_RIGHT_LOGICAL) {
    if (voiceEditing) return "voice_delete";
    if (voiceRecording) return "voice_cancel";
    return "";
  }
  return "";
}

static void emitButtonEvent(LogicalKey key, unsigned long heldMs) {
  if (otaIsActive()) {
    return;
  }
  emitButtonEventWithAction(key, pressActionName(heldMs), heldMs);
}

static void emitButtonEventWithAction(LogicalKey key, const char* action, unsigned long heldMs) {
  if (otaIsActive()) {
    return;
  }
  char line[320];
  const char* voiceIntent = voiceIntentName(key);
  snprintf(
    line,
    sizeof(line),
    "{\"type\":\"button_event\",\"key\":\"%s\",\"action\":\"%s\",\"held_ms\":%lu,\"selected_agent\":%u,\"companion_online\":%s,\"voice_recording\":%s,\"voice_editing\":%s,\"voice_intent\":\"%s\"}",
    logicalKeyName(key),
    action,
    heldMs,
    selectedAgent,
    companionIsOnline() ? "true" : "false",
    voiceRecording ? "true" : "false",
    voiceEditing ? "true" : "false",
    voiceIntent
  );
  Serial.println(line);
  bleGattCommSendLine(line);
}

static void drawKey(LogicalKey key) {
  Arduino_GFX* g = selectLogicalScreen(key);
  AgentState state = agentStateAt(selectedAgent);
  const bool voiceActive = voiceRecording || voiceEditing;

  if (key == KEY_LEFT_LOGICAL) {
    if (voiceRecording) {
      drawEscIcon(g, "STOP");
    } else if (voiceEditing) {
      drawEscIcon(g, "EXIT");
    } else if (state == AGENT_RUNNING) {
      drawEscIcon(g, "STOP");
    } else if (state == AGENT_ERROR) {
      drawEscIcon(g, "RESET");
    } else {
      drawEscIcon(g, "CANCEL");
    }
  } else if (key == KEY_MIDDLE_LOGICAL) {
    if (voiceActive) {
      drawCheckIcon(g);
    } else {
      drawMicIcon(g);
    }
  } else {
    if (voiceRecording || voiceEditing) {
      drawBackspaceIcon(g);
    } else {
      char label[12];
      snprintf(label, sizeof(label), "NEXT %s", agentDisplayName(agentSlots, (selectedAgent + 1) % AGENT_COUNT));
      drawSwitchAgentIcon(g, label);
    }
  }
  deselectScreenKeys();
}

static void drawAllKeys() {
  drawKey(KEY_LEFT_LOGICAL);
  drawKey(KEY_MIDDLE_LOGICAL);
  drawKey(KEY_RIGHT_LOGICAL);
}

static AgentState agentStateAt(uint8_t agentIndex) {
  return ::agentStateAt(agentSlots, agentIndex);
}

static void refreshUi() {
  if (otaIsActive()) {
    drawOtaRound(&roundDisplay(), otaProgressPercent());
    drawOtaRect(rectDisplay(), otaPhase(), otaProgressPercent());
    for (uint8_t i = 0; i < LOGICAL_KEY_COUNT; i++) {
      Arduino_GFX* g = selectLogicalScreen(static_cast<LogicalKey>(i));
      drawEscIcon(g, "WAIT");
      deselectScreenKeys();
    }
    return;
  }
  if (pairingPhase() == PAIRING_PAIRING) {
    // Pairing screen: code on the round display, prompt on the rect display,
    // and confirm/cancel hints on the key screens.
    drawPairingRound(&roundDisplay(), pairingCodeStr());
    drawPairingRect(rectDisplay(), pairingCodeStr());
    Arduino_GFX* gl = selectLogicalScreen(KEY_LEFT_LOGICAL);
    drawEscIcon(gl, "CANCEL");
    deselectScreenKeys();
    Arduino_GFX* gm = selectLogicalScreen(KEY_MIDDLE_LOGICAL);
    drawCheckIcon(gm);
    deselectScreenKeys();
    Arduino_GFX* gr = selectLogicalScreen(KEY_RIGHT_LOGICAL);
    gr->fillScreen(0x0000);
    deselectScreenKeys();
    return;
  }
  drawAllKeys();
  drawRectMetadata(rectDisplay(), agentSlots, selectedAgent, voiceRecording, voiceEditing);
  drawWifiStatusBar(rectDisplay(), wifiModeLabel(), wifiIpAddress(), wifiApSsid(), wifiApPassword());
  drawExprFrame(roundDisplay(), agentStateAt(selectedAgent), voiceRecording, currentExpr, currentFrame, true);
}

static void switchAgent(bool sendHid) {
  if (voiceRecording) {
    hidTap(KEY_ESC);
    delay(DICTATION_COMMIT_DELAY_MS);
    Serial.printf("[VOICE] Agent %d stopped recording before switch\n", selectedAgent + 1);
  }
  selectedAgent = (selectedAgent + 1) % AGENT_COUNT;
  voiceRecording = false;
  voiceEditing = false;
  if (sendHid) {
    sendCommandRightBracket();
  }
  Serial.printf("[AGENT] selected Agent %d\n", selectedAgent + 1);
  refreshUi();
}

static void startVoiceInput() {
  voiceRecording = true;
  voiceEditing = false;
  // System engine: drive macOS dictation with the Control double-tap.
  // Third-party ASR: the companion records via the button_event, so we must not emit the
  // dictation HID (otherwise both system dictation and third-party ASR fire at once).
  if (!voiceEngineIsThirdParty()) {
    sendDoubleControl();
  }
  Serial.printf("[VOICE] Agent %d recording (%s)\n",
                selectedAgent + 1,
                voiceEngineIsThirdParty() ? "third_party" : "system");
  refreshUi();
}

static void sendVoiceInput() {
  // System engine submits with HID Return. Third-party ASR text operations are executed
  // by the companion so they use the same focus path as paste.
  if (!voiceEngineIsThirdParty()) {
    hidTap(KEY_RETURN);
  }
  voiceRecording = false;
  voiceEditing = false;
  if (agentSlots[selectedAgent].occupied) {
    agentSlots[selectedAgent].state = AGENT_RUNNING;
    agentSlots[selectedAgent].lastUpdateMs = millis();
  }
  Serial.printf("[VOICE] Agent %d sent input\n", selectedAgent + 1);
  refreshUi();
}

static void stopDictationForEditing() {
  voiceRecording = false;
  if (voiceEngineIsThirdParty()) {
    voiceEditing = true;
  } else {
    hidTap(KEY_ESC);
    voiceEditing = true;
  }
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

static void handleLeftShort() {
  AgentState state = agentStateAt(selectedAgent);
  if (voiceRecording) {
    stopDictationForEditing();
    return;
  }

  if (voiceEditing) {
    hidTap(KEY_ESC);
    voiceEditing = false;
    Serial.printf("[VOICE] Agent %d exited edit mode\n", selectedAgent + 1);
    refreshUi();
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

static void backspaceFromRecording() {
  voiceRecording = false;
  if (voiceEngineIsThirdParty()) {
    voiceEditing = false;
  } else {
    hidTap(KEY_ESC);
    delay(DICTATION_COMMIT_DELAY_MS);
    voiceEditing = true;
    hidTap(KEY_BACKSPACE);
  }
  Serial.printf("[VOICE] Agent %d stopped dictation and backspaced\n", selectedAgent + 1);
  refreshUi();
}

static void handleRightShort() {
  if (voiceRecording) {
    backspaceFromRecording();
    return;
  }

  if (voiceEditing) {
    if (!voiceEngineIsThirdParty()) {
      hidTap(KEY_BACKSPACE);
    }
    Serial.println("[VOICE] Backspace");
    return;
  }

  switchAgent(true);
}

static void handleRightLong() {
  if (voiceRecording) {
    backspaceFromRecording();
    return;
  }

  if (voiceEditing) {
    if (!voiceEngineIsThirdParty()) {
      hidTap(KEY_BACKSPACE);
    }
    Serial.println("[VOICE] Backspace");
    return;
  }

  handleRightShort();
}

static constexpr uint16_t AGENT_REMOVE_HOLD_MS = 5000;

static void removeCurrentAgentSlot() {
  AgentSlot& agent = agentSlots[selectedAgent];
  clearAgentSlot(agent);
  Serial.printf("[AGENT] Removed slot %d\n", selectedAgent + 1);
  refreshUi();
}

static void handleButtonRelease(LogicalKey key, unsigned long heldMs) {
  if (otaIsActive()) {
    return;
  }
  if (wifiResetSuppressReleases || pairingGestureSuppress) {
    return;
  }

  // While the pairing window is open, the keys mean confirm/cancel only.
  if (pairingPhase() == PAIRING_PAIRING) {
    if (key == KEY_MIDDLE_LOGICAL) {
      pairingConfirm();
      refreshUi();
    } else if (key == KEY_LEFT_LOGICAL) {
      pairingCancel();
      refreshUi();
    }
    return;
  }

  emitButtonEvent(key, heldMs);

  const bool longPress = heldMs >= LONG_PRESS_MS;
  if (key == KEY_LEFT_LOGICAL) {
    handleLeftShort();
  } else if (key == KEY_MIDDLE_LOGICAL) {
    handleMiddleShort();
  } else if (longPress) {
    if (buttons[KEY_RIGHT_LOGICAL].repeatFired) {
      return;
    }
    if (!voiceRecording && !voiceEditing && heldMs >= AGENT_REMOVE_HOLD_MS) {
      removeCurrentAgentSlot();
      return;
    }
    handleRightLong();
  } else {
    handleRightShort();
  }
}

static void handleHeldButton(LogicalKey key, ButtonState& btn, unsigned long now) {
  if (key != KEY_RIGHT_LOGICAL || !voiceEditing || btn.stable != LOW) {
    return;
  }

  if (now - btn.pressedMs < LONG_PRESS_MS) {
    return;
  }

  if (btn.lastRepeatMs != 0 && now - btn.lastRepeatMs < BACKSPACE_REPEAT_MS) {
    return;
  }

  if (voiceEngineIsThirdParty()) {
    emitButtonEventWithAction(KEY_RIGHT_LOGICAL, "repeat", now - btn.pressedMs);
  } else {
    hidTap(KEY_BACKSPACE);
  }
  btn.repeatFired = true;
  btn.lastRepeatMs = millis();
  Serial.println("[VOICE] Backspace repeat");
}

static void pollButtons() {
  if (otaIsActive()) {
    return;
  }
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
        btn.lastRepeatMs = 0;
        btn.repeatFired = false;
      } else {
        handleButtonRelease(key, now - btn.pressedMs);
      }
    }

    handleHeldButton(key, btn, millis());
  }

  bool allPressed = true;
  bool allReleased = true;
  for (uint8_t i = 0; i < LOGICAL_KEY_COUNT; i++) {
    allPressed = allPressed && buttons[i].stable == LOW;
    allReleased = allReleased && buttons[i].stable == HIGH;
  }

  if (wifiResetSuppressReleases && allReleased) {
    wifiResetSuppressReleases = false;
  }

  // Enter pairing mode: hold LEFT + RIGHT together (without MIDDLE) for ~3s.
  // Distinct from the all-three 5s WiFi reset gesture (middle held there).
  const bool leftRightHeld =
      buttons[KEY_LEFT_LOGICAL].stable == LOW &&
      buttons[KEY_RIGHT_LOGICAL].stable == LOW &&
      buttons[KEY_MIDDLE_LOGICAL].stable == HIGH;

  if (pairingGestureSuppress &&
      buttons[KEY_LEFT_LOGICAL].stable == HIGH &&
      buttons[KEY_RIGHT_LOGICAL].stable == HIGH) {
    pairingGestureSuppress = false;
  }

  if (!wifiResetSuppressReleases && !pairingGestureSuppress && leftRightHeld &&
      pairingPhase() != PAIRING_PAIRING &&
      now - buttons[KEY_LEFT_LOGICAL].pressedMs >= PAIRING_ENTER_HOLD_MS &&
      now - buttons[KEY_RIGHT_LOGICAL].pressedMs >= PAIRING_ENTER_HOLD_MS) {
    pairingGestureSuppress = true;
    pairingEnterMode();
    refreshUi();
  }

  if (!wifiResetSuppressReleases && allPressed) {
    bool heldLongEnough = true;
    for (uint8_t i = 0; i < LOGICAL_KEY_COUNT; i++) {
      heldLongEnough = heldLongEnough && now - buttons[i].pressedMs >= WIFI_RESET_HOLD_MS;
    }
    if (heldLongEnough) {
      wifiResetSuppressReleases = true;
      wifiForgetCredentials();
      refreshUi();
    }
  }
}

static void pollWifiStatusUi(unsigned long now) {
  if (pairingPhase() == PAIRING_PAIRING || otaIsActive()) {
    return;
  }
  if (now - lastWifiStatusMs < WIFI_STATUS_INTERVAL_MS) {
    return;
  }
  lastWifiStatusMs = now;
  drawWifiStatusBar(rectDisplay(), wifiModeLabel(), wifiIpAddress(), wifiApSsid(), wifiApPassword());
}

static void pollBleStatus(unsigned long now) {
  if (otaIsActive() || !bleGattCommConnected() || now - lastBleStatusMs < BLE_STATUS_INTERVAL_MS) {
    return;
  }
  lastBleStatusMs = now;

  char line[160];
  snprintf(
    line,
    sizeof(line),
    "{\"type\":\"ble_status\",\"selected_agent\":%u,\"companion_online\":%s}",
    selectedAgent,
    companionIsOnline() ? "true" : "false"
  );
  bleGattCommSendLine(line);
}

static void pollOtaUi(unsigned long now) {
  if (!otaIsActive()) {
    lastOtaProgress = 255;
    return;
  }
  uint8_t progress = otaProgressPercent();
  if (progress != lastOtaProgress || now - lastOtaUiMs >= 250) {
    lastOtaProgress = progress;
    lastOtaUiMs = now;
    refreshUi();
  }
}

void setup() {
  Serial.begin(115200);

  agentRegistryBegin();
  hidBegin();
  beginDisplayHardware();
  wifiConfigBegin();
  bleHidBegin();
  bleGattCommSetRegistry(agentSlots, &selectedAgent);
  bleGattCommBegin(handleAgentRegistryLine);
  pairingBegin();

  for (uint8_t i = 0; i < LOGICAL_KEY_COUNT; i++) {
    buttons[i].lastRaw = digitalRead(pinForLogical(static_cast<LogicalKey>(i)));
    buttons[i].stable = buttons[i].lastRaw;
  }

  lastFrameMs = millis();
  refreshUi();

  Serial.println("Kiro KB ready (5-screen agent controller)");
  printKeyWiring();
}

void loop() {
  unsigned long now = millis();
  wifiConfigLoop();

  if (!otaIsActive() && pairingPhase() != PAIRING_PAIRING && now - lastFrameMs >= FRAME_INTERVAL_MS) {
    lastFrameMs = now;
    currentFrame = (currentFrame + 1) % EXPR_FRAME_COUNT;
    drawExprFrame(roundDisplay(), agentStateAt(selectedAgent), voiceRecording, currentExpr, currentFrame, false);
  }

  if (pollAgentRegistrySerial(Serial, agentSlots, selectedAgent)) {
    refreshUi();
  }
  if (pairingPoll(now)) {
    refreshUi();
  }
  otaLoop();
  pollOtaUi(now);
  pollWifiStatusUi(now);
  pollBleStatus(now);
  pollButtons();
}
