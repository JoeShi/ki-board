#include "agent_registry.h"
#include <ArduinoJson.h>
#include <cstring>

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

static bool handleRegistryLine(AgentSlot* slots, uint8_t& selectedAgent, const char* line) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) {
    return false;
  }

  const char* type = doc["type"] | "";
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
  static char line[256];
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
        changed = handleRegistryLine(slots, selectedAgent, line) || changed;
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
