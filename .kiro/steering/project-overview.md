---
inclusion: always
---

# 项目概述：Kiro 快捷键盘

## 项目目标
一个桌面快捷键盘，通过 USB HID 连接 PC，模拟键盘输入触发 Kiro/VS Code 的常用操作。
3个带屏幕的机械按键显示当前功能，1个圆形LCD显示 Kiro Agent 状态，1个矩形LCD显示 Agent 运行详情。

## 硬件配置
- **主控**: Waveshare ESP32-S3-DEV-KIT-N16R8 (WiFi + BLE 5, Xtensa 双核 240MHz, 16MB Flash, 8MB PSRAM)
- **按键模块**: 3x Waveshare 0.85inch ScreenKey Module B
  - 驱动: ST7735, 128x128 IPS, 65K色
  - 接口: SPI (DIN, CLK, CS, DC, RST, PWM) + 1路按键GPIO
  - 连接器: GH1.25 9PIN
- **圆形LCD**: Waveshare 0.71inch LCD Module
  - 驱动: GC9D01, 160x160 IPS, 65K色, 圆形显示
  - 接口: SPI (DIN, CLK, CS, DC, RST, BL), 8PIN
  - 用途: 显示 Kiro Agent 状态图标
- **矩形LCD**: Waveshare 1.47inch LCD Module
  - 驱动: ST7789V3, 172x320 IPS, 65K色
  - 接口: SPI (DIN, CLK, CS, DC, RST, BL), PH2.0 8PIN
  - 用途: 显示 Agent 运行详情
- **连接方式**: 定制PCB板 (ESP32_5SCREEN_V0.1) 统一连接所有模块

## 通信方案：USB HID（当前）/ BLE HID（备用）
- ESP32-S3 支持原生 USB OTG，采用 Arduino 内置 `USB.h` + `USBHIDKeyboard.h` 模拟标准 USB 键盘
- USB 线即插即用，无需配对，无需 PC 端驱动软件，延迟低、连接稳定
- 适合稳定触发 macOS 语音输入（双击 Control）等系统级快捷键
- BLE HID 方案（HijelHID_BLEKeyboard）代码保留在 `ble_hid.*`，如需无线可切换

## 技术栈
- **框架**: Arduino (ESP32 Arduino Core)，适合快速原型开发
- **USB HID**: Arduino ESP32 内置 `USB.h` + `USBHIDKeyboard.h`（当前方案）
- **BLE HID（备用）**: HijelHID_BLEKeyboard 库 (基于 NimBLE)
- **屏幕驱动**: Arduino_GFX 库 (支持 ST7735 / GC9D01 / ST7789V3)
- **图形界面**: LVGL (图标/文字显示)
- **Web配置**: ESPAsyncWebServer + ArduinoJson + LittleFS
- **网络发现**: mDNS (http://kirokb.local)
- **构建工具**: PlatformIO

## 引脚分配 (ESP32-S3, PCB: ESP32_5SCREEN_V0.1)

### SPI 总线分组
- **SPI-A** (3个ScreenKey共享): MOSI=GPIO9, CLK=GPIO10
- **SPI-B** (圆形LCD + 矩形LCD共享): MOSI=GPIO12, CLK=GPIO11

### ScreenKey 按键模块 (SPI-A)

| 屏幕 | CS | DC | RST | PWM(BL) | KEY |
|------|----|----|-----|---------|-----|
| ScreenKey 1 (J1) | GPIO13 | GPIO18 | GPIO4 | GPIO42 | GPIO1 |
| ScreenKey 2 (J11) | GPIO14 | GPIO8 | GPIO38 | GPIO43 | GPIO2 |
| ScreenKey 3 (J12) | GPIO15 | GPIO7 | GPIO39 | GPIO44 | GPIO21 |

### 独立LCD (SPI-B)

| 屏幕 | CS | DC | RST | PWM(BL) |
|------|----|----|-----|---------|
| 圆形LCD 0.71" (J5) | GPIO16 | GPIO6 | GPIO40 | GPIO47 |
| 矩形LCD 1.47" (J6) | GPIO17 | GPIO5 | GPIO41 | GPIO48 |

## 默认按键映射

| 输入 | 功能 | 发送的快捷键 |
|------|------|-------------|
| Key1 | 打开AI聊天 | Ctrl+L |
| Key2 | 接受AI建议 | Tab |
| Key3 | 拒绝AI建议 | Escape |

## 开发阶段

1. **阶段1 - 基础验证**: 单屏驱动 + 单按键检测
2. **阶段2 - 多屏驱动**: 双SPI总线 5 屏驱动 (ST7735 + GC9D01 + ST7789V3)
3. **阶段3 - BLE HID**: 蓝牙键盘配对 + 按键映射发送
4. **阶段4 - Web配置系统**: WiFi AP/STA + Web Server + 前端配置页面 + NVS持久化
5. **阶段5 - 整合优化**: BLE+WiFi共存 + 实时预览 + 低功耗

## 代码模块结构 (src/)
- **config.h**: 设备名/硬件参数/消抖阈值等常量
- **pins.h**: ESP32-S3 引脚定义
- **keymap.h/.cpp**: KeyAction动作定义 + 默认映射 + JSON序列化 + NVS持久化
- **keyregistry.h/.cpp**: 键名<->键码双向映射表 (Web UI 用名称, 设备用键码)
- **buttons.h/.cpp**: 3个按键扫描, 消抖+短按/长按检测
- **ble_hid.h/.cpp**: HijelHID_BLEKeyboard封装, 按KeyAction发送快捷键
- **display.h/.cpp**: 5屏管理 (Arduino_GFX), 双SPI总线+独立CS
- **webconfig.h/.cpp**: WiFi AP + ESPAsyncWebServer + REST API
- **webpage.h**: 前端配置页面 (HTML/CSS/JS 嵌入 PROGMEM)
- **main.cpp**: 主循环整合所有模块

## 开发进度
- [x] 阶段3核心: 按键+BLE HID
- [x] 阶段1: 屏幕显示
- [x] 阶段4: Web配置系统 (WiFi AP + REST API + 前端 + NVS持久化)
- [x] 完整系统编译通过 (RAM 15.3%, Flash 46.9%)
- [ ] 待硬件到位后烧录验证

## Web 配置使用方式
1. 设备上电启动 WiFi AP 热点 "KiroKeyboard-Config" (开放无密码)
2. PC/手机连接该热点, 浏览器打开 http://192.168.4.1
3. 页面加载当前按键映射, 修改后点"保存并应用"
4. 配置 POST 到 /api/config -> 解析应用 -> 存入NVS -> 刷新屏幕
5. REST API: GET /api/keys (可用键), GET/POST /api/config (映射)

## 屏幕驱动注意事项 (硬件验证时)
- ST7735 128x128 偏移 ST7735_COL_OFFSET/ROW_OFFSET 当前设为(2,3), 若显示错位需调整
- 双SPI总线: SPI-A (3×ScreenKey, GPIO9/10), SPI-B (圆形+矩形LCD, GPIO12/11)
- 每屏独立 CS/DC/RST/BL, 用 Arduino_ESP32SPI + FSPI
- ScreenKey 确认为 ST7735 驱动 (非GC9107)

## Web 配置方案

### 架构
- ESP32-S3 内置 Web Server，PC 浏览器直接访问配置
- 首次启动: AP 热点模式 (SSID: KiroKeyboard-Config)，访问 192.168.4.1
- 配网后: STA 模式加入局域网，通过 mDNS 访问 http://kirokb.local
- 前端页面存储在 LittleFS 分区
- WiFi 和 BLE 共存运行

### REST API

| 接口 | 方法 | 功能 |
|------|------|------|
| /api/config | GET | 获取当前所有按键映射 |
| /api/config | POST | 保存新的按键映射 |
| /api/keys | GET | 获取可用快捷键列表 |
| /api/reboot | POST | 重启设备应用新配置 |
| /api/wifi | POST | 配置WiFi连接信息 |

### 配置数据结构 (JSON)
```json
{
  "keys": [
    {"id": 0, "label": "AI Chat", "icon": "chat", "action": {"type": "hotkey", "keys": ["ctrl", "l"]}},
    {"id": 1, "label": "Accept", "icon": "check", "action": {"type": "hotkey", "keys": ["tab"]}},
    {"id": 2, "label": "Reject", "icon": "close", "action": {"type": "hotkey", "keys": ["escape"]}}
  ]
}
```
