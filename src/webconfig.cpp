/**
 * webconfig.cpp - Web 配置服务实现
 *
 * 路由:
 *   GET  /            -> 配置页面 (HTML)
 *   GET  /api/keys    -> 可用键名 + 修饰键名 (供下拉列表)
 *   GET  /api/config  -> 当前按键映射 (JSON)
 *   POST /api/config  -> 保存新映射 (JSON) -> NVS, 并触发回调
 */

#include "webconfig.h"
#include "webpage.h"
#include "config.h"
#include "keymap.h"
#include "keyregistry.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

static AsyncWebServer s_server(80);
static ConfigChangedCallback s_onChanged = nullptr;

// POST body 累积缓冲 (异步分块到达)
static String s_postBody;

// GET /api/keys: 返回可用键名和修饰键名
static void handleGetKeys(AsyncWebServerRequest* request) {
    JsonDocument doc;
    JsonArray keys = doc["keys"].to<JsonArray>();
    const char* names[128];
    uint16_t n = allKeyNames(names, 128);
    for (uint16_t i = 0; i < n; i++) keys.add(names[i]);

    JsonArray mods = doc["modifiers"].to<JsonArray>();
    const char* modNames[8];
    uint8_t mn = allModifierNames(modNames, 8);
    for (uint8_t i = 0; i < mn; i++) mods.add(modNames[i]);

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

// GET /api/config: 返回当前映射
static void handleGetConfig(AsyncWebServerRequest* request) {
    request->send(200, "application/json", keymapToJson());
}

// POST /api/config: 应用并保存新映射
static void handlePostConfigBody(AsyncWebServerRequest* request, uint8_t* data,
                                 size_t len, size_t index, size_t total) {
    if (index == 0) s_postBody = "";
    s_postBody.concat((const char*)data, len);

    // 收齐所有分块后处理
    if (index + len == total) {
        bool ok = keymapFromJson(s_postBody);
        if (ok) {
            keymapSave();
            if (s_onChanged) s_onChanged();
        }
        s_postBody = "";

        JsonDocument doc;
        doc["ok"] = ok;
        String out;
        serializeJson(doc, out);
        request->send(ok ? 200 : 400, "application/json", out);
    }
}

void webConfigBegin(ConfigChangedCallback onChanged) {
    s_onChanged = onChanged;

    // 启动 AP 热点 (开放, 无密码)
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID);
    Serial.printf("[WEB] AP: %s  IP: %s\n",
                  WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());

    // 配置页面
    s_server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send_P(200, "text/html", INDEX_HTML);
    });

    // REST API
    s_server.on("/api/keys", HTTP_GET, handleGetKeys);
    s_server.on("/api/config", HTTP_GET, handleGetConfig);
    s_server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest* request) {},  // onRequest (body 在下面处理)
        nullptr,                                 // onUpload
        handlePostConfigBody);                   // onBody

    s_server.onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "Not found");
    });

    s_server.begin();
    Serial.println("[WEB] Server started. Open http://192.168.4.1");
}
