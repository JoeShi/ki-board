#ifndef DISPLAY_HARDWARE_H
#define DISPLAY_HARDWARE_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

enum LogicalKey : uint8_t {
  KEY_LEFT_LOGICAL = 0,
  KEY_MIDDLE_LOGICAL = 1,
  KEY_RIGHT_LOGICAL = 2,
  LOGICAL_KEY_COUNT = 3
};

struct KeyBinding {
  uint8_t screenIndex;
  int keyPin;
};

Arduino_GFX& roundDisplay();
Arduino_GFX& rectDisplay();

KeyBinding bindingForLogical(LogicalKey key);
int pinForLogical(LogicalKey key);

void beginDisplayHardware();
Arduino_GFX* selectLogicalScreen(LogicalKey key);
void deselectScreenKeys();
void printKeyWiring();

#endif // DISPLAY_HARDWARE_H
