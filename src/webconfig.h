/**
 * webconfig.h - Web 配置服务
 *
 * ESP32-C6 启动 WiFi AP 热点 + 异步 Web 服务器,
 * 提供配置页面和 REST API, 让用户在浏览器中自定义按键映射。
 */

#ifndef WEBCONFIG_H
#define WEBCONFIG_H

#include <Arduino.h>

// 配置变更回调: 当 Web 端保存了新配置后调用 (供主程序刷新屏幕等)
typedef void (*ConfigChangedCallback)();

// 启动 Web 配置服务 (AP 模式)
//   onChanged: 配置保存后的回调 (可为 nullptr)
void webConfigBegin(ConfigChangedCallback onChanged);

#endif // WEBCONFIG_H
