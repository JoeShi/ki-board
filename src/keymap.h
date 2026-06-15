/**
 * keymap.h - 按键动作定义与默认映射
 *
 * 定义一个"动作"为一组修饰键 + 一个主键 (或一个媒体键),
 * 通过 BLE HID 发送给主机, 触发 Kiro/VS Code 的功能。
 */

#ifndef KEYMAP_H
#define KEYMAP_H

#include <Arduino.h>
#include "config.h"

// 动作类型
enum class ActionType : uint8_t {
    NONE = 0,       // 无动作
    HOTKEY,         // 普通快捷键 (修饰键 + 主键)
    MEDIA,          // 媒体/消费类键 (音量等)
    DOUBLE_TAP,     // 连按两次同一键 (如双击 Control 唤起语音输入)
};

// 一个按键动作的定义
struct KeyAction {
    ActionType type;
    uint8_t    modifiers;   // KEY_MOD_* 位掩码 (HOTKEY 用)
    uint8_t    keycode;     // KEY_* 主键   (HOTKEY 用)
    uint16_t   mediaCode;   // MEDIA_* 媒体键 (MEDIA 用)
    const char* label;      // 屏幕显示用的简短标签
};

// 构造 HOTKEY 动作的便捷宏
#define ACTION_HOTKEY(mod, key, lbl)  { ActionType::HOTKEY, (mod), (key), 0, (lbl) }
#define ACTION_MEDIA(media, lbl)      { ActionType::MEDIA, 0, 0, (media), (lbl) }
#define ACTION_DOUBLE_TAP(key, lbl)   { ActionType::DOUBLE_TAP, 0, (key), 0, (lbl) }
#define ACTION_NONE()                 { ActionType::NONE, 0, 0, 0, "" }

// === 全局键映射配置 (运行时可被Web配置覆盖) ===

// 3个按键的动作
extern KeyAction g_keyActions[NUM_KEYS];

// 加载默认映射 (上电时调用)
void keymapLoadDefaults();

// === 持久化与 JSON (Web配置用) ===

// 从 NVS 加载配置; 若无存储则加载默认值。返回 true 表示从NVS成功加载
bool keymapLoad();

// 把当前映射保存到 NVS。返回 true 表示成功
bool keymapSave();

// 把当前映射序列化为 JSON 字符串
String keymapToJson();

// 从 JSON 字符串解析并应用到当前映射。返回 true 表示成功
bool keymapFromJson(const String& json);

#endif // KEYMAP_H
