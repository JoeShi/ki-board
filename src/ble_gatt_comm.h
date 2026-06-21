#ifndef BLE_GATT_COMM_H
#define BLE_GATT_COMM_H

#include <Arduino.h>
#include "agent_registry.h"

typedef bool (*BleGattLineHandler)(const char* line, AgentSlot* slots, uint8_t& selectedAgent, Print& output, CompanionChannel channel);

void bleGattCommBegin(BleGattLineHandler handler);
void bleGattCommSetRegistry(AgentSlot* slots, uint8_t* selectedAgent);
void bleGattCommSendLine(const char* line);
bool bleGattCommConnected();
const char* bleGattCommBoardId();
const char* bleGattCommDeviceName();

#endif // BLE_GATT_COMM_H
