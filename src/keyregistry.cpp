/**
 * keyregistry.cpp - 键名 <-> 键码 映射表实现
 */

#include "keyregistry.h"
#include <string.h>
#include <HijelHID_BLEKeyboard.h>

struct KeyEntry {
    const char* name;
    uint8_t     code;
};

// 可用按键表 (名称 -> 键码)。名称用于 Web UI 显示与配置。
static const KeyEntry KEY_TABLE[] = {
    // 字母
    {"A", KEY_A}, {"B", KEY_B}, {"C", KEY_C}, {"D", KEY_D}, {"E", KEY_E},
    {"F", KEY_F}, {"G", KEY_G}, {"H", KEY_H}, {"I", KEY_I}, {"J", KEY_J},
    {"K", KEY_K}, {"L", KEY_L}, {"M", KEY_M}, {"N", KEY_N}, {"O", KEY_O},
    {"P", KEY_P}, {"Q", KEY_Q}, {"R", KEY_R}, {"S", KEY_S}, {"T", KEY_T},
    {"U", KEY_U}, {"V", KEY_V}, {"W", KEY_W}, {"X", KEY_X}, {"Y", KEY_Y},
    {"Z", KEY_Z},
    // 数字
    {"1", KEY_1}, {"2", KEY_2}, {"3", KEY_3}, {"4", KEY_4}, {"5", KEY_5},
    {"6", KEY_6}, {"7", KEY_7}, {"8", KEY_8}, {"9", KEY_9}, {"0", KEY_0},
    // 功能键
    {"F1", KEY_F1}, {"F2", KEY_F2}, {"F3", KEY_F3}, {"F4", KEY_F4},
    {"F5", KEY_F5}, {"F6", KEY_F6}, {"F7", KEY_F7}, {"F8", KEY_F8},
    {"F9", KEY_F9}, {"F10", KEY_F10}, {"F11", KEY_F11}, {"F12", KEY_F12},
    // 常用控制键
    {"Enter", KEY_RETURN}, {"Escape", KEY_ESCAPE}, {"Backspace", KEY_BACKSPACE},
    {"Tab", KEY_TAB}, {"Space", KEY_SPACE},
    {"Up", KEY_UP}, {"Down", KEY_DOWN}, {"Left", KEY_LEFT}, {"Right", KEY_RIGHT},
    {"PageUp", KEY_PAGE_UP}, {"PageDown", KEY_PAGE_DOWN},
    {"Home", KEY_HOME}, {"End", KEY_END}, {"Insert", KEY_INSERT}, {"Delete", KEY_DELETE},
    // 符号
    {"Minus", KEY_MINUS}, {"Equal", KEY_EQUAL}, {"Period", KEY_DOT}, {"Comma", KEY_COMMA},
    {"Slash", KEY_SLASH}, {"Backslash", KEY_BACKSLASH}, {"Grave", KEY_GRAVE},
};

static const uint16_t KEY_TABLE_LEN = sizeof(KEY_TABLE) / sizeof(KEY_TABLE[0]);

struct ModEntry {
    const char* name;
    uint8_t     bit;
};

static const ModEntry MOD_TABLE[] = {
    {"ctrl",  KEY_MOD_LCTRL},
    {"shift", KEY_MOD_LSHIFT},
    {"alt",   KEY_MOD_LALT},
    {"gui",   KEY_MOD_LGUI},   // Win/Cmd
};

static const uint8_t MOD_TABLE_LEN = sizeof(MOD_TABLE) / sizeof(MOD_TABLE[0]);

uint8_t keyCodeFromName(const char* name) {
    if (!name) return 0;
    for (uint16_t i = 0; i < KEY_TABLE_LEN; i++) {
        if (strcasecmp(name, KEY_TABLE[i].name) == 0) {
            return KEY_TABLE[i].code;
        }
    }
    return 0;
}

const char* keyNameFromCode(uint8_t code) {
    if (code == 0) return "";
    for (uint16_t i = 0; i < KEY_TABLE_LEN; i++) {
        if (KEY_TABLE[i].code == code) {
            return KEY_TABLE[i].name;
        }
    }
    return "";
}

uint8_t modifierBitFromName(const char* name) {
    if (!name) return 0;
    for (uint8_t i = 0; i < MOD_TABLE_LEN; i++) {
        if (strcasecmp(name, MOD_TABLE[i].name) == 0) {
            return MOD_TABLE[i].bit;
        }
    }
    return 0;
}

uint8_t modifierNamesFromMask(uint8_t mask, const char** names, uint8_t maxNames) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < MOD_TABLE_LEN && n < maxNames; i++) {
        if (mask & MOD_TABLE[i].bit) {
            names[n++] = MOD_TABLE[i].name;
        }
    }
    return n;
}

uint16_t allKeyNames(const char** names, uint16_t maxNames) {
    uint16_t n = (KEY_TABLE_LEN < maxNames) ? KEY_TABLE_LEN : maxNames;
    for (uint16_t i = 0; i < n; i++) {
        names[i] = KEY_TABLE[i].name;
    }
    return n;
}

uint8_t allModifierNames(const char** names, uint8_t maxNames) {
    uint8_t n = (MOD_TABLE_LEN < maxNames) ? MOD_TABLE_LEN : maxNames;
    for (uint8_t i = 0; i < n; i++) {
        names[i] = MOD_TABLE[i].name;
    }
    return n;
}
