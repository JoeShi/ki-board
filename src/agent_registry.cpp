#include "agent_registry.h"
#include "hid_actions.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <cstring>

#include "ota_manager.h"
#include "pairing.h"

// Must exceed the companion heartbeat interval (5s) with margin so the board
// does not flap the companion between online/offline between heartbeats.
static constexpr unsigned long COMPANION_ONLINE_GRACE_MS = 8000;
static unsigned long s_companionLastSeenMs = 0;
// Voice engine selected by the companion. When false (default) the board owns
// dictation and emits the macOS Control double-tap on the middle key. When the
// companion switches to Doubao ASR it records itself, so the board must NOT emit
// the dictation HID.
static bool s_voiceEngineDoubao = false;
static constexpr const char* KEYMAP_NAMESPACE = "kirokb";
static constexpr const char* KEYMAP_KEY = "companion_keymap";
static constexpr const char* VOICE_ENGINE_KEY = "voice_engine";
static constexpr const char* DEFAULT_KEYMAP_JSON =
  "{\"keys\":["
  "{\"label\":\"ESC\",\"action_type\":\"hotkey\",\"key\":\"Escape\",\"modifiers\":[]},"
  "{\"label\":\"Voice\",\"action_type\":\"voice\",\"key\":\"Voice\",\"modifiers\":[]},"
  "{\"label\":\"Next\",\"action_type\":\"agent_next\",\"key\":\"RightBracket\",\"modifiers\":[\"gui\"]}"
  "]}";

const char* agentStateName(AgentState state) {
  switch (state) {
    case AGENT_IDLE: return "Idle";
    case AGENT_RUNNING: return "Running";
    case AGENT_ERROR: return "Error";
  }
  return "Unknown";
}

AgentState agentStateAt(const AgentSlot* slots, uint8_t agentIndex) {
  return slots[agentIndex].occupied ? slots[agentIndex].state : AGENT_IDLE;
}

const char* agentDisplayName(const AgentSlot* slots, uint8_t agentIndex) {
  if (!slots[agentIndex].occupied || slots[agentIndex].name[0] == '\0') {
    return "EMPTY";
  }
  return slots[agentIndex].name;
}

void clearAgentSlot(AgentSlot& agent) {
  agent.name[0] = '\0';
  agent.sessionId[0] = '\0';
  agent.cwd[0] = '\0';
  agent.state = AGENT_IDLE;
  agent.occupied = false;
  agent.lastUpdateMs = 0;
}

void agentRegistryBegin() {
  Preferences prefs;
  if (prefs.begin(KEYMAP_NAMESPACE, true)) {
    String engine = prefs.getString(VOICE_ENGINE_KEY, "system");
    s_voiceEngineDoubao = (engine == "doubao");
    prefs.end();
  }
  Serial.printf("[VOICE] stored engine -> %s\n", s_voiceEngineDoubao ? "doubao" : "system");
}

void companionMarkSeen() {
  s_companionLastSeenMs = millis();
}

bool companionIsOnline() {
  return s_companionLastSeenMs != 0 &&
         millis() - s_companionLastSeenMs < COMPANION_ONLINE_GRACE_MS;
}

bool voiceEngineIsDoubao() {
  return s_voiceEngineDoubao;
}

static uint8_t findAgentSlotByName(const AgentSlot* slots, const char* name) {
  for (uint8_t i = 0; i < AGENT_COUNT; i++) {
    if (slots[i].occupied && strcmp(slots[i].name, name) == 0) {
      return i;
    }
  }
  return AGENT_COUNT;
}

static uint8_t findAgentSlotBySessionId(const AgentSlot* slots, const char* sid) {
  if (!sid || !sid[0]) return AGENT_COUNT;
  for (uint8_t i = 0; i < AGENT_COUNT; i++) {
    if (slots[i].occupied && strcmp(slots[i].sessionId, sid) == 0) {
      return i;
    }
  }
  return AGENT_COUNT;
}

static uint8_t chooseAgentSlot(const AgentSlot* slots, const char* name, const char* sessionId) {
  uint8_t bySession = findAgentSlotBySessionId(slots, sessionId);
  if (bySession < AGENT_COUNT) return bySession;

  uint8_t byName = findAgentSlotByName(slots, name);
  if (byName < AGENT_COUNT) return byName;

  for (uint8_t i = 0; i < AGENT_COUNT; i++) {
    if (!slots[i].occupied) return i;
  }

  uint8_t oldest = 0;
  unsigned long oldestUpdate = slots[0].lastUpdateMs;
  for (uint8_t i = 1; i < AGENT_COUNT; i++) {
    if (slots[i].lastUpdateMs < oldestUpdate) {
      oldest = i;
      oldestUpdate = slots[i].lastUpdateMs;
    }
  }
  return oldest;
}

static bool applyRegistryEvent(AgentSlot* slots, uint8_t& selectedAgent,
                               const char* agentName, AgentState state,
                               const char* sessionId, const char* cwd) {
  if (!agentName || !agentName[0]) {
    return false;
  }

  uint8_t slot = chooseAgentSlot(slots, agentName, sessionId);
  AgentSlot& agent = slots[slot];
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
  return true;
}

bool handleAgentRegistryLine(const char* line, AgentSlot* slots, uint8_t& selectedAgent, Print& output) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) {
    return false;
  }

  const char* type = doc["type"] | "";
  if (strcmp(type, "hello") == 0 || strcmp(type, "companion_hello") == 0) {
    JsonDocument response;
    response["type"] = "hello_ack";
    response["protocol"] = 1;
    response["fw"] = "0.2.0";
    JsonArray capabilities = response["capabilities"].to<JsonArray>();
    capabilities.add("usb_cdc");
    capabilities.add("ble_gatt");
    capabilities.add("keymap");
    capabilities.add("voice_state");
    capabilities.add("ota");
    capabilities.add("ota_usb_cdc");
    capabilities.add("ota_ble_gatt");
    serializeJson(response, output);
    output.println();
    companionMarkSeen();
    return false;
  }

  if (strcmp(type, "ping") == 0) {
    JsonDocument response;
    response["type"] = "pong";
    response["protocol"] = 1;
    serializeJson(response, output);
    output.println();
    companionMarkSeen();
    return false;
  }

  if (strcmp(type, "companion_ping") == 0) {
    companionMarkSeen();
    return false;
  }

  if (otaHandleCommand(doc, output)) {
    companionMarkSeen();
    return false;
  }

  if (strcmp(type, "get_keymap") == 0) {
    Preferences prefs;
    String keymap = DEFAULT_KEYMAP_JSON;
    if (prefs.begin(KEYMAP_NAMESPACE, true)) {
      keymap = prefs.getString(KEYMAP_KEY, DEFAULT_KEYMAP_JSON);
      prefs.end();
    }
    JsonDocument response;
    deserializeJson(response, keymap);
    response["type"] = "keymap_response";
    response["request_id"] = doc["request_id"] | "";
    response["ok"] = true;
    serializeJson(response, output);
    output.println();
    companionMarkSeen();
    return false;
  }

  if (strcmp(type, "set_keymap") == 0) {
    JsonDocument stored;
    stored["keys"] = doc["keys"];
    String raw;
    serializeJson(stored, raw);

    bool ok = false;
    Preferences prefs;
    if (prefs.begin(KEYMAP_NAMESPACE, false)) {
      ok = prefs.putString(KEYMAP_KEY, raw) > 0;
      prefs.end();
    }

    JsonDocument response;
    response["type"] = "keymap_response";
    response["request_id"] = doc["request_id"] | "";
    response["ok"] = ok;
    if (!ok) {
      response["error"] = "NVS write failed";
    }
    response["keys"] = doc["keys"];
    serializeJson(response, output);
    output.println();
    companionMarkSeen();
    return false;
  }

  if (strcmp(type, "set_hid_output") == 0) {
    const char* mode = doc["mode"] | "usb";
    HidOutputMode m = (strcmp(mode, "ble") == 0) ? HID_OUTPUT_BLE : HID_OUTPUT_USB;
    hidSetOutputMode(m);
    JsonDocument response;
    response["type"] = "hid_output_response";
    response["request_id"] = doc["request_id"] | "";
    response["ok"] = true;
    response["mode"] = (m == HID_OUTPUT_BLE) ? "ble" : "usb";
    serializeJson(response, output);
    output.println();
    companionMarkSeen();
    return false;
  }

  if (strcmp(type, "get_hid_output") == 0) {
    JsonDocument response;
    response["type"] = "hid_output_response";
    response["request_id"] = doc["request_id"] | "";
    response["ok"] = true;
    response["mode"] = (hidGetOutputMode() == HID_OUTPUT_BLE) ? "ble" : "usb";
    serializeJson(response, output);
    output.println();
    companionMarkSeen();
    return false;
  }

  if (strcmp(type, "voice_engine") == 0) {
    const char* engine = doc["engine"] | "system";
    s_voiceEngineDoubao = (strcmp(engine, "doubao") == 0);
    bool persisted = false;
    Preferences prefs;
    if (prefs.begin(KEYMAP_NAMESPACE, false)) {
      persisted = prefs.putString(VOICE_ENGINE_KEY, s_voiceEngineDoubao ? "doubao" : "system") > 0;
      prefs.end();
    }
    JsonDocument response;
    response["type"] = "voice_engine_response";
    response["ok"] = persisted;
    response["engine"] = s_voiceEngineDoubao ? "doubao" : "system";
    response["persisted"] = persisted;
    serializeJson(response, output);
    output.println();
    Serial.printf("[VOICE] engine -> %s\n", s_voiceEngineDoubao ? "doubao" : "system");
    companionMarkSeen();
    return false;
  }

  if (strcmp(type, "agent_state") != 0) {
    return false;
  }

  const char* stateText = doc["state"] | "";
  AgentState state = AGENT_IDLE;
  if (strcmp(stateText, "running") == 0) {
    state = AGENT_RUNNING;
  } else if (strcmp(stateText, "error") == 0) {
    state = AGENT_ERROR;
  }

  return applyRegistryEvent(
    slots,
    selectedAgent,
    doc["agent_name"] | "",
    state,
    doc["session_id"] | "",
    doc["cwd"] | ""
  );
}

bool pollAgentRegistrySerial(Stream& serial, AgentSlot* slots, uint8_t& selectedAgent) {
  static char line[1024];
  static size_t len = 0;
  bool changed = false;

  while (serial.available() > 0) {
    int ch = serial.read();
    if (ch < 0) {
      break;
    }
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      line[len] = '\0';
      if (len > 0 && line[0] == '{') {
        if (pairingHandleLine(line, serial, PAIR_TRANSPORT_USB) == PAIR_LINE_FORWARD) {
          changed = handleAgentRegistryLine(line, slots, selectedAgent, serial) || changed;
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

  return changed;
}
