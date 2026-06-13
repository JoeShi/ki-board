/**
 * keymap.cpp - 默认按键映射实现
 */

#include "keymap.h"
#include "keyregistry.h"
#include <HijelHID_BLEKeyboard.h>   // KEY_* / KEY_MOD_* 常量
#include <BLEHIDMediaKeys.h>        // MEDIA_* 常量
#include <ArduinoJson.h>
#include <Preferences.h>

#define NVS_NAMESPACE  "kirokb"
#define NVS_KEY_CONFIG "keymap"

// 全局映射定义
KeyAction  g_keyActions[NUM_KEYS];
KeyAction  g_encoderShortPress;
KeyAction  g_encoderLongPress;
EncoderMode g_encoderModes[NUM_ENCODER_MODES];

void keymapLoadDefaults() {
    // --- 4个按键 ---
    // Key1: 语音输入 (双击 Control 唤起)
    g_keyActions[0] = ACTION_DOUBLE_TAP(KEY_LEFT_CTRL, "Voice");
    // Key2: 接受AI建议 (Tab)
    g_keyActions[1] = ACTION_HOTKEY(0, KEY_TAB, "Accept");
    // Key3: 拒绝AI建议 (Escape)
    g_keyActions[2] = ACTION_HOTKEY(0, KEY_ESCAPE, "Reject");
    // Key4: 命令面板 (Ctrl+Shift+P)
    g_keyActions[3] = ACTION_HOTKEY(KEY_MOD_LCTRL | KEY_MOD_LSHIFT, KEY_P, "Command");

    // --- 旋钮按压 ---
    // 短按: 切换旋钮模式
    g_encoderShortPress = ACTION_MODE_SWITCH("Mode");
    // 长按: 保存文件 (Ctrl+S)
    g_encoderLongPress  = ACTION_HOTKEY(KEY_MOD_LCTRL, KEY_S, "Save");

    // --- 旋钮3种模式 ---
    // 模式1: 上下滚动
    g_encoderModes[0] = {
        ACTION_HOTKEY(0, KEY_DOWN, "Down"),   // 顺时针向下
        ACTION_HOTKEY(0, KEY_UP, "Up"),       // 逆时针向上
        "Scroll"
    };
    // 模式2: 撤销/重做
    g_encoderModes[1] = {
        ACTION_HOTKEY(KEY_MOD_LCTRL, KEY_Y, "Redo"),   // 顺时针重做
        ACTION_HOTKEY(KEY_MOD_LCTRL, KEY_Z, "Undo"),   // 逆时针撤销
        "Undo/Redo"
    };
    // 模式3: 切换标签页
    g_encoderModes[2] = {
        ACTION_HOTKEY(KEY_MOD_LCTRL, KEY_PAGE_DOWN, "Next"),  // 顺时针下一个
        ACTION_HOTKEY(KEY_MOD_LCTRL, KEY_PAGE_UP, "Prev"),    // 逆时针上一个
        "Tabs"
    };
}

// === JSON 序列化辅助 ===

// 静态标签缓冲: JSON 解析出的 label 需要持久存储 (KeyAction.label 是指针)
// 为每个可配置项预留一块缓冲区
#define LABEL_BUF_LEN 16
static char s_keyLabels[NUM_KEYS][LABEL_BUF_LEN];
static char s_encShortLabel[LABEL_BUF_LEN];
static char s_encLongLabel[LABEL_BUF_LEN];
static char s_modeLabels[NUM_ENCODER_MODES][LABEL_BUF_LEN];
static char s_modeCwLabels[NUM_ENCODER_MODES][LABEL_BUF_LEN];
static char s_modeCcwLabels[NUM_ENCODER_MODES][LABEL_BUF_LEN];

// 把一个 KeyAction 写入 JSON 对象
static void actionToJson(JsonObject obj, const KeyAction& a) {
    obj["label"] = a.label;
    switch (a.type) {
        case ActionType::HOTKEY: {
            obj["type"] = "hotkey";
            obj["key"] = keyNameFromCode(a.keycode);
            JsonArray mods = obj["modifiers"].to<JsonArray>();
            const char* names[8];
            uint8_t n = modifierNamesFromMask(a.modifiers, names, 8);
            for (uint8_t i = 0; i < n; i++) mods.add(names[i]);
            break;
        }
        case ActionType::MEDIA:
            obj["type"] = "media";
            obj["media"] = a.mediaCode;
            break;
        case ActionType::MODE_SWITCH:
            obj["type"] = "mode_switch";
            break;
        default:
            obj["type"] = "none";
            break;
    }
}

// 从 JSON 对象解析一个 KeyAction, label 存入提供的缓冲
static KeyAction actionFromJson(JsonObjectConst obj, char* labelBuf) {
    KeyAction a = ACTION_NONE();

    const char* label = obj["label"] | "";
    strncpy(labelBuf, label, LABEL_BUF_LEN - 1);
    labelBuf[LABEL_BUF_LEN - 1] = '\0';
    a.label = labelBuf;

    const char* type = obj["type"] | "none";
    if (strcmp(type, "hotkey") == 0) {
        a.type = ActionType::HOTKEY;
        a.keycode = keyCodeFromName(obj["key"] | "");
        uint8_t mods = 0;
        for (JsonVariantConst m : obj["modifiers"].as<JsonArrayConst>()) {
            mods |= modifierBitFromName(m.as<const char*>());
        }
        a.modifiers = mods;
    } else if (strcmp(type, "media") == 0) {
        a.type = ActionType::MEDIA;
        a.mediaCode = obj["media"] | 0;
    } else if (strcmp(type, "mode_switch") == 0) {
        a.type = ActionType::MODE_SWITCH;
    }
    return a;
}

String keymapToJson() {
    JsonDocument doc;

    JsonArray keys = doc["keys"].to<JsonArray>();
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        actionToJson(keys.add<JsonObject>(), g_keyActions[i]);
    }

    JsonObject enc = doc["encoder"].to<JsonObject>();
    actionToJson(enc["shortPress"].to<JsonObject>(), g_encoderShortPress);
    actionToJson(enc["longPress"].to<JsonObject>(), g_encoderLongPress);

    JsonArray modes = enc["modes"].to<JsonArray>();
    for (uint8_t i = 0; i < NUM_ENCODER_MODES; i++) {
        JsonObject mo = modes.add<JsonObject>();
        mo["label"] = g_encoderModes[i].label;
        actionToJson(mo["cw"].to<JsonObject>(), g_encoderModes[i].cw);
        actionToJson(mo["ccw"].to<JsonObject>(), g_encoderModes[i].ccw);
    }

    String out;
    serializeJson(doc, out);
    return out;
}

bool keymapFromJson(const String& json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[KEYMAP] JSON parse error: %s\n", err.c_str());
        return false;
    }

    // 按键
    JsonArrayConst keys = doc["keys"].as<JsonArrayConst>();
    uint8_t i = 0;
    for (JsonObjectConst k : keys) {
        if (i >= NUM_KEYS) break;
        g_keyActions[i] = actionFromJson(k, s_keyLabels[i]);
        i++;
    }

    // 旋钮
    JsonObjectConst enc = doc["encoder"].as<JsonObjectConst>();
    if (!enc.isNull()) {
        if (!enc["shortPress"].isNull())
            g_encoderShortPress = actionFromJson(enc["shortPress"], s_encShortLabel);
        if (!enc["longPress"].isNull())
            g_encoderLongPress = actionFromJson(enc["longPress"], s_encLongLabel);

        JsonArrayConst modes = enc["modes"].as<JsonArrayConst>();
        uint8_t m = 0;
        for (JsonObjectConst mo : modes) {
            if (m >= NUM_ENCODER_MODES) break;
            const char* ml = mo["label"] | "";
            strncpy(s_modeLabels[m], ml, LABEL_BUF_LEN - 1);
            s_modeLabels[m][LABEL_BUF_LEN - 1] = '\0';
            g_encoderModes[m].label = s_modeLabels[m];
            g_encoderModes[m].cw  = actionFromJson(mo["cw"],  s_modeCwLabels[m]);
            g_encoderModes[m].ccw = actionFromJson(mo["ccw"], s_modeCcwLabels[m]);
            m++;
        }
    }

    Serial.println("[KEYMAP] Applied config from JSON");
    return true;
}

// === NVS 持久化 ===

bool keymapLoad() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {   // 只读
        keymapLoadDefaults();
        return false;
    }
    String json = prefs.getString(NVS_KEY_CONFIG, "");
    prefs.end();

    if (json.length() == 0) {
        Serial.println("[KEYMAP] No saved config, loading defaults");
        keymapLoadDefaults();
        return false;
    }

    if (!keymapFromJson(json)) {
        Serial.println("[KEYMAP] Saved config invalid, loading defaults");
        keymapLoadDefaults();
        return false;
    }

    Serial.println("[KEYMAP] Loaded config from NVS");
    return true;
}

bool keymapSave() {
    String json = keymapToJson();
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {  // 读写
        Serial.println("[KEYMAP] NVS open failed");
        return false;
    }
    size_t written = prefs.putString(NVS_KEY_CONFIG, json);
    prefs.end();

    Serial.printf("[KEYMAP] Saved %d bytes to NVS\n", written);
    return written > 0;
}
