#ifndef AGENT_REGISTRY_H
#define AGENT_REGISTRY_H

#include <Arduino.h>

static constexpr uint8_t AGENT_COUNT = 4;

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

const char* agentStateName(AgentState state);
AgentState agentStateAt(const AgentSlot* slots, uint8_t agentIndex);
const char* agentDisplayName(const AgentSlot* slots, uint8_t agentIndex);
void clearAgentSlot(AgentSlot& agent);
bool pollAgentRegistrySerial(Stream& serial, AgentSlot* slots, uint8_t& selectedAgent);
bool handleAgentRegistryLine(const char* line, AgentSlot* slots, uint8_t& selectedAgent, Print& output);
bool companionIsOnline();
void companionMarkSeen();

#endif // AGENT_REGISTRY_H
