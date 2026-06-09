/**
 * keyregistry.h - 键名 <-> 键码 映射表
 *
 * Web 配置界面用键名 (如 "L", "Tab", "Escape") 描述按键,
 * 设备内部用 HID 键码。此模块提供双向转换, 以及修饰键名转换。
 */

#ifndef KEYREGISTRY_H
#define KEYREGISTRY_H

#include <Arduino.h>

// 键名 -> HID 键码 (找不到返回 0 = KEY_NONE)
uint8_t keyCodeFromName(const char* name);

// HID 键码 -> 键名 (找不到返回 "")
const char* keyNameFromCode(uint8_t code);

// 修饰键名 -> 修饰位 (如 "ctrl"->0x01, 找不到返回 0)
uint8_t modifierBitFromName(const char* name);

// 修饰位掩码 -> 修饰键名数组, 写入 names[], 返回数量
uint8_t modifierNamesFromMask(uint8_t mask, const char** names, uint8_t maxNames);

// 获取所有可用键名 (供 Web UI 下拉列表), 返回数量
uint16_t allKeyNames(const char** names, uint16_t maxNames);

// 获取所有修饰键名, 返回数量
uint8_t allModifierNames(const char** names, uint8_t maxNames);

#endif // KEYREGISTRY_H
