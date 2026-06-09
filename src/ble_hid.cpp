/**
 * ble_hid.cpp - BLE HID 键盘封装实现
 */

#include "ble_hid.h"
#include "config.h"
#include <HijelHID_BLEKeyboard.h>
#include <BLEHIDMediaKeys.h>

// BLE 键盘实例
static HijelHID_BLEKeyboard s_keyboard(BLE_DEVICE_NAME, "Kiro", 100);

void bleHidBegin() {
    s_keyboard.begin();
    Serial.println("[BLE] HID keyboard advertising as: " BLE_DEVICE_NAME);
}

bool bleHidConnected() {
    return s_keyboard.isConnected();
}

bool bleHidSendAction(const KeyAction& action) {
    if (!s_keyboard.isConnected()) {
        return false;
    }

    switch (action.type) {
        case ActionType::HOTKEY:
            // tap(keycode, modifiers) 自动按下并释放
            s_keyboard.tap(action.keycode, action.modifiers);
            Serial.printf("[BLE] Hotkey sent: %s (mod=0x%02X key=0x%02X)\n",
                          action.label, action.modifiers, action.keycode);
            return true;

        case ActionType::MEDIA:
            s_keyboard.tap(action.mediaCode);
            Serial.printf("[BLE] Media sent: %s (0x%04X)\n",
                          action.label, action.mediaCode);
            return true;

        case ActionType::MODE_SWITCH:
        case ActionType::NONE:
        default:
            // 这些类型不发送按键, 由上层处理
            return false;
    }
}
