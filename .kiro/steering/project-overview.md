---
inclusion: always
---

# 项目概述：Kiro 快捷键盘

## 项目目标
一个桌面快捷键盘，通过 BLE 蓝牙连接 PC，模拟键盘输入触发 Kiro/VS Code 的常用操作。
4个带屏幕的机械按键显示当前功能，1个旋转编码器提供连续控制。

## 硬件配置
- **主控**: ESP32-C6-DEV-KIT-N16 (WiFi 6 + BLE 5, RISC-V 160MHz, 16MB Flash)
- **按键模块**: 4x Waveshare 0.85inch ScreenKey Module B
  - 驱动: ST7735, 128x128 IPS, 65K色
  - 接口: SPI (MOSI, CLK, CS, DC, RST, BL) + 1路按键GPIO
  - 连接器: GH1.25 9PIN
- **旋钮**: EC11 旋转编码器 (A相, B相, 按压开关)
- **连接方式**: 原型阶段使用杜邦线 + 面包板/PCB万用洞洞板

## 通信方案：BLE HID
- ESP32-C6 不支持原生 USB Device，采用 BLE HID Keyboard 方案
- 蓝牙无线连接，无需额外硬件或PC端驱动软件
- 首次使用需配对，之后自动重连
- 延迟约 10-30ms，对快捷键操作完全可接受

## 技术栈
- **框架**: Arduino (ESP32 Arduino Core)，适合快速原型开发
- **BLE HID**: ESP32-BLE-Keyboard 库
- **屏幕驱动**: TFT_eSPI 库 (ST7735)
- **图形界面**: LVGL (图标/文字显示)
- **旋转编码器**: ESP32Encoder 库 (硬件中断方式)
- **Web配置**: ESPAsyncWebServer + ArduinoJson + LittleFS
- **网络发现**: mDNS (http://kirokb.local)
- **构建工具**: PlatformIO

## 引脚分配 (ESP32-C6)

| 功能 | GPIO | 说明 |
|------|------|------|
| SPI_MOSI (共享) | GPIO7 | 4屏共用 |
| SPI_CLK (共享) | GPIO6 | 4屏共用 |
| LCD_CS0 | GPIO8 | 屏幕1片选 |
| LCD_CS1 | GPIO9 | 屏幕2片选 |
| LCD_CS2 | GPIO10 | 屏幕3片选 |
| LCD_CS3 | GPIO11 | 屏幕4片选 |
| LCD_DC (共享) | GPIO4 | 数据/命令切换 |
| LCD_RST (共享) | GPIO5 | 统一复位 |
| LCD_BL (共享) | GPIO3 | 统一背光控制 |
| KEY0 | GPIO0 | 按键1 |
| KEY1 | GPIO1 | 按键2 |
| KEY2 | GPIO2 | 按键3 |
| KEY3 | GPIO15 | 按键4 |
| EC11_A | GPIO18 | 编码器A相 |
| EC11_B | GPIO19 | 编码器B相 |
| EC11_SW | GPIO20 | 编码器按压 |

## 默认按键映射

| 输入 | 功能 | 发送的快捷键 |
|------|------|-------------|
| Key1 | 打开AI聊天 | Ctrl+L |
| Key2 | 接受AI建议 | Tab |
| Key3 | 拒绝AI建议 | Escape |
| Key4 | 命令面板 | Ctrl+Shift+P |
| 旋钮短按 | 切换旋钮模式 | — |
| 旋钮长按 | 保存文件 | Ctrl+S |
| 旋钮旋转(模式1) | 上下滚动 | ↑ / ↓ |
| 旋钮旋转(模式2) | 撤销/重做 | Ctrl+Z / Ctrl+Y |
| 旋钮旋转(模式3) | 切换标签页 | Ctrl+PgUp / Ctrl+PgDn |

## 开发阶段

1. **阶段1 - 基础验证**: 单屏驱动 + 单按键检测
2. **阶段2 - 多屏+编码器**: 4路CS切换 + EC11中断驱动 + LVGL显示
3. **阶段3 - BLE HID**: 蓝牙键盘配对 + 按键映射发送
4. **阶段4 - Web配置系统**: WiFi AP/STA + Web Server + 前端配置页面 + NVS持久化
5. **阶段5 - 整合优化**: BLE+WiFi共存 + 实时预览 + 低功耗

## 代码模块结构 (src/)
- **config.h**: 设备名/硬件参数/消抖阈值等常量
- **pins.h**: ESP32-C6 引脚定义
- **keymap.h/.cpp**: KeyAction动作定义 + 默认映射 + JSON序列化 + NVS持久化
- **keyregistry.h/.cpp**: 键名<->键码双向映射表 (Web UI 用名称, 设备用键码)
- **buttons.h/.cpp**: 5个按键扫描 (4屏幕键+1旋钮键), 消抖+短按/长按检测
- **encoder_input.h/.cpp**: 旋钮封装 (RotaryEncoderPCNT), 旋转方向检测+3模式切换
- **ble_hid.h/.cpp**: HijelHID_BLEKeyboard封装, 按KeyAction发送快捷键
- **display.h/.cpp**: 4个ST7735屏幕管理 (Arduino_GFX), 共享SPI+独立CS
- **webconfig.h/.cpp**: WiFi AP + ESPAsyncWebServer + REST API
- **webpage.h**: 前端配置页面 (HTML/CSS/JS 嵌入 PROGMEM)
- **main.cpp**: 主循环整合所有模块

## 开发进度
- [x] 阶段3核心: 按键+旋钮+BLE HID
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
- 4屏共享 DC/RST/BL + 独立CS, 用 Arduino_ESP32SPI(is_shared_interface=true) + FSPI
- ScreenKey 确认为 ST7735 驱动 (非GC9107)

## Web 配置方案

### 架构
- ESP32-C6 内置 Web Server，PC 浏览器直接访问配置
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
    {"id": 2, "label": "Reject", "icon": "close", "action": {"type": "hotkey", "keys": ["escape"]}},
    {"id": 3, "label": "Command", "icon": "terminal", "action": {"type": "hotkey", "keys": ["ctrl", "shift", "p"]}}
  ],
  "encoder": {
    "press_short": {"type": "mode_switch"},
    "press_long": {"type": "hotkey", "keys": ["ctrl", "s"]},
    "modes": [
      {"label": "Scroll", "cw": {"keys": ["down"]}, "ccw": {"keys": ["up"]}},
      {"label": "Undo", "cw": {"keys": ["ctrl", "y"]}, "ccw": {"keys": ["ctrl", "z"]}},
      {"label": "Tabs", "cw": {"keys": ["ctrl", "pagedown"]}, "ccw": {"keys": ["ctrl", "pageup"]}}
    ]
  }
}
```
