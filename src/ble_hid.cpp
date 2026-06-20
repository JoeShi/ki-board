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
    s_keyboard.setSecurityMode(BLEKeyboardSecurity::JustWorks);
    s_keyboard.setLogLevel(HIDLogLevel::Normal);
    s_keyboard.begin();
    // Stop the library's own advertising — we manage advertising centrally in
    // ble_gatt_comm so we can control when HID is discoverable (only in pairing
    // mode) vs when only the companion service is advertised.
    NimBLEDevice::stopAdvertising();
    Serial.println("[BLE-HID] service registered (not yet discoverable)");
}

void bleHidSetDiscoverable(bool discoverable) {
    // Rebuild advertising data: always include our custom Kiro service UUID;
    // add the HID service UUID only when in pairing/discoverable mode.
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->reset();
    adv->addServiceUUID("6b69726f-6b62-0001-8000-00805f9b34fb");
    if (discoverable) {
        adv->addServiceUUID(NimBLEUUID(static_cast<uint16_t>(0x1812))); // HID
    }
    adv->enableScanResponse(true);
    adv->setAppearance(0x03C1); // Keyboard
    NimBLEDevice::startAdvertising();
    Serial.printf("[BLE-HID] discoverable=%s\n", discoverable ? "true" : "false");
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

        case ActionType::DOUBLE_TAP:
            // 连按两次同一键, 间隔 50ms
            s_keyboard.tap(action.keycode, 0);
            delay(50);
            s_keyboard.tap(action.keycode, 0);
            Serial.printf("[BLE] DoubleTap sent: %s (key=0x%02X)\n",
                          action.label, action.keycode);
            return true;

        case ActionType::MEDIA:
            s_keyboard.tap(action.mediaCode);
            Serial.printf("[BLE] Media sent: %s (0x%04X)\n",
                          action.label, action.mediaCode);
            return true;

        case ActionType::NONE:
        default:
            // 这些类型不发送按键, 由上层处理
            return false;
    }
}

bool bleHidSendKey(uint8_t keycode) {
    if (!s_keyboard.isConnected()) return false;
    s_keyboard.tap(keycode, 0);
    return true;
}
