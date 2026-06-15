#include "hid_actions.h"
#include <USB.h>

static constexpr uint16_t HID_TAP_MS = 70;

static USBHIDKeyboard Keyboard;

void hidBegin() {
  Keyboard.begin();
  USB.begin();
}

void hidTap(uint8_t key) {
  Keyboard.press(key);
  delay(HID_TAP_MS);
  Keyboard.releaseAll();
  delay(30);
}

void sendCommandRightBracket() {
  Keyboard.press(KEY_LEFT_GUI);
  Keyboard.press(']');
  delay(HID_TAP_MS);
  Keyboard.releaseAll();
}

void sendDoubleControl() {
  hidTap(KEY_LEFT_CTRL);
  delay(90);
  hidTap(KEY_LEFT_CTRL);
}

void sendClearInput() {
  Keyboard.press(KEY_LEFT_GUI);
  Keyboard.press('a');
  delay(HID_TAP_MS);
  Keyboard.releaseAll();
  delay(40);
  hidTap(KEY_BACKSPACE);
}
