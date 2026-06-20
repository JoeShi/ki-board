#ifndef HID_ACTIONS_H
#define HID_ACTIONS_H

#include <Arduino.h>
#include <USBHIDKeyboard.h>

enum HidOutputMode : uint8_t {
  HID_OUTPUT_USB = 0,
  HID_OUTPUT_BLE = 1,
};

void hidBegin();
void hidSetOutputMode(HidOutputMode mode);
HidOutputMode hidGetOutputMode();

void hidTap(uint8_t key);
void sendCommandRightBracket();
void sendDoubleControl();
void sendClearInput();

#endif // HID_ACTIONS_H
