#include "hid_actions.h"
#include "ble_hid.h"
#include <USB.h>
#include <Preferences.h>
#include <BLEHIDKeys.h>

static constexpr uint16_t HID_TAP_MS = 70;
static constexpr const char* NVS_NS = "kirokb";
static constexpr const char* NVS_HID_MODE = "hid_mode";

static USBHIDKeyboard UsbKb;
static HidOutputMode s_mode = HID_OUTPUT_USB;

static void loadMode() {
  Preferences prefs;
  if (prefs.begin(NVS_NS, true)) {
    s_mode = static_cast<HidOutputMode>(prefs.getUChar(NVS_HID_MODE, HID_OUTPUT_USB));
    prefs.end();
  }
}

void hidBegin() {
  UsbKb.begin();
  USB.begin();
  loadMode();
  Serial.printf("[HID] output mode: %s\n", s_mode == HID_OUTPUT_BLE ? "BLE" : "USB");
}

void hidSetOutputMode(HidOutputMode mode) {
  s_mode = mode;
  Preferences prefs;
  if (prefs.begin(NVS_NS, false)) {
    prefs.putUChar(NVS_HID_MODE, mode);
    prefs.end();
  }
  Serial.printf("[HID] output mode set: %s\n", mode == HID_OUTPUT_BLE ? "BLE" : "USB");
}

HidOutputMode hidGetOutputMode() {
  return s_mode;
}

void hidTap(uint8_t key) {
  if (s_mode == HID_OUTPUT_BLE) {
    bleHidSendKey(key);
  } else {
    UsbKb.press(key);
    delay(HID_TAP_MS);
    UsbKb.releaseAll();
    delay(30);
  }
}

void sendCommandRightBracket() {
  if (s_mode == HID_OUTPUT_BLE) {
    // BLE HID: GUI + ]
    KeyAction action = {ActionType::HOTKEY, KEY_MOD_LGUI, KEY_RIGHTBRACE, 0, "Cmd+]"};
    bleHidSendAction(action);
  } else {
    UsbKb.press(KEY_LEFT_GUI);
    UsbKb.press(']');
    delay(HID_TAP_MS);
    UsbKb.releaseAll();
  }
}

void sendCommandLeftBracket() {
  if (s_mode == HID_OUTPUT_BLE) {
    // BLE HID: GUI + [
    KeyAction action = {ActionType::HOTKEY, KEY_MOD_LGUI, KEY_LEFTBRACE, 0, "Cmd+["};
    bleHidSendAction(action);
  } else {
    UsbKb.press(KEY_LEFT_GUI);
    UsbKb.press('[');
    delay(HID_TAP_MS);
    UsbKb.releaseAll();
  }
}

void sendDoubleControl() {
  hidTap(KEY_LEFT_CTRL);
  delay(90);
  hidTap(KEY_LEFT_CTRL);
}

void sendClearInput() {
  if (s_mode == HID_OUTPUT_BLE) {
    KeyAction selectAll = {ActionType::HOTKEY, KEY_MOD_LGUI, KEY_A, 0, "Cmd+A"};
    bleHidSendAction(selectAll);
    delay(40);
    bleHidSendKey(KEY_BACKSPACE);
  } else {
    UsbKb.press(KEY_LEFT_GUI);
    UsbKb.press('a');
    delay(HID_TAP_MS);
    UsbKb.releaseAll();
    delay(40);
    UsbKb.press(KEY_BACKSPACE);
    delay(HID_TAP_MS);
    UsbKb.releaseAll();
    delay(30);
  }
}
