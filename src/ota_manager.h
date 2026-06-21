#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>

bool otaHandleCommand(const JsonDocument& request, Print& output);
bool otaIsActive();
uint8_t otaProgressPercent();
const char* otaPhase();
void otaLoop();

#endif // OTA_MANAGER_H
