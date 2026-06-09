/**
 * buttons.h - 物理按键扫描 (消抖 + 边沿检测)
 *
 * 处理 4 个 ScreenKey 按键 + 1 个旋钮按压键, 共 5 个数字输入。
 * 提供"按下瞬间"(短按) 和 "长按" 事件。
 */

#ifndef BUTTONS_H
#define BUTTONS_H

#include <Arduino.h>

// 按键事件类型
enum class ButtonEvent : uint8_t {
    NONE = 0,
    SHORT_PRESS,    // 短按 (释放时若未达长按阈值)
    LONG_PRESS,     // 长按 (按住超过阈值, 触发一次)
};

// 单个按键的运行状态 (内部使用)
struct ButtonState {
    uint8_t  pin;
    bool     lastReading;       // 上次原始电平 (true=按下)
    bool     stableState;       // 消抖后的稳定状态
    uint32_t lastChangeMs;      // 上次电平变化时间
    uint32_t pressStartMs;      // 按下起始时间
    bool     longFired;         // 本次按下是否已触发长按
};

// 初始化所有按键 GPIO (内部上拉)
void buttonsBegin();

// 扫描所有按键, 返回每个按键本次的事件。
// events 数组长度需 >= 按键总数, 按键顺序: [0..NUM_KEYS-1]=屏幕按键, [NUM_KEYS]=旋钮按压
// 返回按键总数
uint8_t buttonsScan(ButtonEvent* events, uint8_t maxEvents);

// 按键总数 (4屏幕键 + 1旋钮键)
#define TOTAL_BUTTONS  (NUM_KEYS + 1)
// 旋钮按压键在事件数组中的索引
#define ENCODER_BTN_INDEX  (NUM_KEYS)

#endif // BUTTONS_H
