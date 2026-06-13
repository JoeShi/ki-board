#ifndef HID_ACTIONS_H
#define HID_ACTIONS_H

#include <Arduino.h>
#include <USBHIDKeyboard.h>

void hidBegin();
void hidTap(uint8_t key);
void sendCommandRightBracket();
void sendDoubleControl();
void sendClearInput();

#endif // HID_ACTIONS_H
