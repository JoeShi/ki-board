/**
 * Kiro 快捷键盘 - 主程序
 *
 * 硬件: ESP32-C6-DEV-KIT-N16
 * 功能: 4个屏幕按键 + 1个旋转编码器, 通过BLE HID发送快捷键触发Kiro功能
 *
 * 当前实现 (阶段3整合): 按键 + 旋钮 + BLE HID
 * (屏幕显示在后续阶段加入)
 */

#include <Arduino.h>
#include "config.h"
#include "keymap.h"
#include "buttons.h"
#include "encoder_input.h"
#include "ble_hid.h"
#include "display.h"
#include "webconfig.h"

void handleButtonEvent(uint8_t index, ButtonEvent ev);
void handleEncoderTurn(EncoderTurn turn);
void refreshAllKeyDisplays();
void onConfigChanged();

// 4个按键的显示颜色
static const uint16_t s_keyColors[NUM_KEYS] = {
    COLOR_CYAN, COLOR_GREEN, COLOR_RED, COLOR_ORANGE
};

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("====================================");
    Serial.println("  " DEVICE_NAME " booting...");
    Serial.println("====================================");

    // 1. 加载按键映射 (优先从 NVS, 否则默认值)
    keymapLoad();
    Serial.println("[MAIN] Keymap loaded");

    // 2. 初始化输入
    buttonsBegin();
    encoderBegin();

    // 3. 初始化屏幕并显示各按键功能
    displayBegin();
    refreshAllKeyDisplays();

    // 4. 启动 BLE HID
    bleHidBegin();

    // 5. 启动 Web 配置服务 (WiFi AP + HTTP)
    webConfigBegin(onConfigChanged);

    Serial.println("[MAIN] Setup complete. Waiting for BLE connection...");
}

// 刷新所有按键屏幕, 显示当前映射的功能标签
void refreshAllKeyDisplays() {
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        displayShowKey(i, g_keyActions[i].label, s_keyColors[i]);
    }
}

// Web 端保存新配置后的回调: 刷新屏幕显示
void onConfigChanged() {
    Serial.println("[MAIN] Config changed, refreshing displays");
    refreshAllKeyDisplays();
}

void loop() {
    // --- 扫描按键 ---
    ButtonEvent events[TOTAL_BUTTONS];
    uint8_t n = buttonsScan(events, TOTAL_BUTTONS);
    for (uint8_t i = 0; i < n; i++) {
        if (events[i] != ButtonEvent::NONE) {
            handleButtonEvent(i, events[i]);
        }
    }

    // --- 读取旋钮旋转 ---
    EncoderTurn turn = encoderPoll();
    if (turn != EncoderTurn::NONE) {
        handleEncoderTurn(turn);
    }

    delay(5);
}

// 处理按键事件 (索引 0..NUM_KEYS-1 为屏幕按键, NUM_KEYS 为旋钮按压)
void handleButtonEvent(uint8_t index, ButtonEvent ev) {
    if (index < NUM_KEYS) {
        // 屏幕按键: 短按发送对应动作
        if (ev == ButtonEvent::SHORT_PRESS) {
            Serial.printf("[MAIN] Key%d short press\n", index);
            displayFlashKey(index);                            // 按下反馈
            bleHidSendAction(g_keyActions[index]);
            displayShowKey(index, g_keyActions[index].label,   // 恢复显示
                           s_keyColors[index]);
        }
        // (屏幕按键长按暂未定义功能, 预留)
    } else if (index == ENCODER_BTN_INDEX) {
        // 旋钮按压
        if (ev == ButtonEvent::SHORT_PRESS) {
            // 短按: 切换模式
            Serial.println("[MAIN] Encoder short press -> switch mode");
            if (g_encoderShortPress.type == ActionType::MODE_SWITCH) {
                uint8_t m = encoderNextMode();
                Serial.printf("[MAIN] Now in mode: %s\n", g_encoderModes[m].label);
                displayShowEncoderMode(g_encoderModes[m].label);
            } else {
                bleHidSendAction(g_encoderShortPress);
            }
        } else if (ev == ButtonEvent::LONG_PRESS) {
            // 长按: 发送长按动作 (默认保存)
            Serial.println("[MAIN] Encoder long press");
            bleHidSendAction(g_encoderLongPress);
        }
    }
}

// 处理旋钮旋转
void handleEncoderTurn(EncoderTurn turn) {
    uint8_t mode = encoderCurrentMode();
    const EncoderMode& m = g_encoderModes[mode];

    if (turn == EncoderTurn::CW) {
        Serial.printf("[MAIN] Encoder CW (mode=%s)\n", m.label);
        bleHidSendAction(m.cw);
    } else if (turn == EncoderTurn::CCW) {
        Serial.printf("[MAIN] Encoder CCW (mode=%s)\n", m.label);
        bleHidSendAction(m.ccw);
    }
}
