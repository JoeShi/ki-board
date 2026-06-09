/**
 * buttons.cpp - 物理按键扫描实现
 *
 * 采用轮询 + 时间戳消抖。按键接法: GPIO 内部上拉, 按下接地 (低电平=按下)。
 */

#include "buttons.h"
#include "pins.h"
#include "config.h"

// 5个按键的引脚: 4个屏幕按键 + 1个旋钮按压
static const uint8_t s_pins[TOTAL_BUTTONS] = {
    PIN_KEY0, PIN_KEY1, PIN_KEY2, PIN_KEY3, PIN_EC11_SW
};

static ButtonState s_buttons[TOTAL_BUTTONS];

void buttonsBegin() {
    for (uint8_t i = 0; i < TOTAL_BUTTONS; i++) {
        pinMode(s_pins[i], INPUT_PULLUP);
        s_buttons[i].pin          = s_pins[i];
        s_buttons[i].lastReading  = false;
        s_buttons[i].stableState  = false;
        s_buttons[i].lastChangeMs = 0;
        s_buttons[i].pressStartMs = 0;
        s_buttons[i].longFired    = false;
    }
    Serial.printf("[BTN] Initialized %d buttons\n", TOTAL_BUTTONS);
}

uint8_t buttonsScan(ButtonEvent* events, uint8_t maxEvents) {
    uint32_t now = millis();
    uint8_t count = (maxEvents < TOTAL_BUTTONS) ? maxEvents : TOTAL_BUTTONS;

    for (uint8_t i = 0; i < count; i++) {
        events[i] = ButtonEvent::NONE;
        ButtonState& b = s_buttons[i];

        // 读取原始电平: LOW = 按下 (内部上拉)
        bool reading = (digitalRead(b.pin) == LOW);

        // 电平变化, 重置消抖计时
        if (reading != b.lastReading) {
            b.lastChangeMs = now;
            b.lastReading = reading;
        }

        // 消抖: 电平稳定超过阈值才接受
        if ((now - b.lastChangeMs) >= KEY_DEBOUNCE_MS) {
            if (reading != b.stableState) {
                b.stableState = reading;

                if (b.stableState) {
                    // 刚按下: 记录起始时间
                    b.pressStartMs = now;
                    b.longFired = false;
                } else {
                    // 刚释放: 若未触发过长按, 则发出短按事件
                    if (!b.longFired) {
                        events[i] = ButtonEvent::SHORT_PRESS;
                    }
                }
            }
        }

        // 长按检测: 持续按住超过阈值, 触发一次
        if (b.stableState && !b.longFired &&
            (now - b.pressStartMs) >= KEY_LONG_PRESS_MS) {
            b.longFired = true;
            events[i] = ButtonEvent::LONG_PRESS;
        }
    }

    return count;
}
