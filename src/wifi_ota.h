#ifndef WIFI_OTA_H
#define WIFI_OTA_H

#include <Arduino.h>

/// OTA 状态枚举
enum WifiOtaState : uint8_t {
    WIFI_OTA_IDLE = 0,       // 未激活
    WIFI_OTA_AP_STARTING,    // 正在启动 AP
    WIFI_OTA_AP_READY,       // AP 就绪，等待连接
    WIFI_OTA_UPLOADING,      // 固件上传中
    WIFI_OTA_VERIFYING,      // SHA-256 校验中
    WIFI_OTA_SUCCESS,        // 升级成功，等待重启
    WIFI_OTA_ERROR,          // 错误状态
    WIFI_OTA_TIMEOUT         // 超时状态
};

/// 错误类型枚举
enum WifiOtaError : uint8_t {
    WIFI_OTA_ERR_NONE = 0,
    WIFI_OTA_ERR_AP_FAIL,        // AP 启动失败
    WIFI_OTA_ERR_TOO_LARGE,      // 固件超过分区大小
    WIFI_OTA_ERR_EMPTY_FILE,     // 文件为空
    WIFI_OTA_ERR_WRITE_FAIL,     // 写入失败
    WIFI_OTA_ERR_SHA256_MISMATCH,// SHA-256 校验失败
    WIFI_OTA_ERR_TIMEOUT,        // 传输超时
    WIFI_OTA_ERR_UPDATE_END      // Update.end() 失败
};

/// 启动 WiFi OTA 模式（由三键检测触发调用）
bool wifiOtaBegin();

/// 主循环中调用，处理超时和状态转换
void wifiOtaLoop();

/// 查询 WiFi OTA 是否处于激活状态
bool wifiOtaIsActive();

/// 获取当前 OTA 状态
WifiOtaState wifiOtaGetState();

/// 获取当前进度百分比 (0-100)
uint8_t wifiOtaProgress();

/// 获取当前错误类型
WifiOtaError wifiOtaGetError();

/// 获取 OTA AP 的 SSID
String wifiOtaApSsid();

/// 获取 OTA AP 的密码
String wifiOtaApPassword();

/// Get the current OTA phase description string for display
const char* wifiOtaPhase();

/// Get error message string for display
const char* wifiOtaErrorMessage();

#endif // WIFI_OTA_H
