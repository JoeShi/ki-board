/**
 * 项目配置常量
 */

#ifndef CONFIG_H
#define CONFIG_H

// 设备信息
#define DEVICE_NAME         "Kiro Keyboard"
#define BLE_DEVICE_NAME     "Kiro KB"
#define WIFI_AP_SSID        "KiroKeyboard-Config"
#define WIFI_AP_PASS        ""  // 开放AP, 无密码
#define MDNS_HOSTNAME       "kirokb"

// 硬件参数
#define NUM_KEYS            4
#define NUM_ENCODER_MODES   3
#define LCD_WIDTH           128
#define LCD_HEIGHT          128

// 按键参数
#define KEY_DEBOUNCE_MS     50
#define KEY_LONG_PRESS_MS   800

// 编码器参数
#define ENCODER_DEBOUNCE_MS 5

#endif // CONFIG_H
