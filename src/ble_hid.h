/**
 * ble_hid.h - BLE HID 键盘封装
 *
 * 封装 HijelHID_BLEKeyboard, 提供基于 KeyAction 的高层发送接口。
 */

#ifndef BLE_HID_H
#define BLE_HID_H

#include "keymap.h"

// 初始化 BLE HID, 开始广播
void bleHidBegin();

// 是否已连接到主机
bool bleHidConnected();

// 发送一个按键动作 (HOTKEY/MEDIA/DOUBLE_TAP), MODE_SWITCH/NONE 会被忽略
// 返回 true 表示实际发送了按键
bool bleHidSendAction(const KeyAction& action);

// 发送单个按键 (无修饰键)
bool bleHidSendKey(uint8_t keycode);

#endif // BLE_HID_H
