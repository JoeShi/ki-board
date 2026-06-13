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
#define NUM_KEYS            3
#define NUM_SCREENKEYS      3

// ScreenKey 屏幕参数 (ST7735)
#define SK_LCD_WIDTH        128
#define SK_LCD_HEIGHT       128

// 圆形LCD参数 (GC9D01)
#define RLCD_WIDTH          160
#define RLCD_HEIGHT         160

// 矩形LCD参数 (ST7789V3)
#define RECT_LCD_WIDTH      172
#define RECT_LCD_HEIGHT     320

// 按键参数
#define KEY_DEBOUNCE_MS     50
#define KEY_LONG_PRESS_MS   800

// 背光参数 (PWM 调光)
#define BL_PWM_FREQ         5000    // PWM 频率 (Hz)
#define BL_PWM_RESOLUTION   8       // PWM 分辨率 (位), 8位=0..255
#define BL_DEFAULT_LEVEL    200     // 默认亮度 (0..255), 约78%, 兼顾省电与可视

#endif // CONFIG_H
