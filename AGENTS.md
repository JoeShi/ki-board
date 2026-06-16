# 项目概述：Kiro 快捷键盘

> 本文件是整个项目的总体描述（给 AI agent 阅读）。
> 硬件/开发框架细节见 `.kiro/steering/dev-environment.md`，
> 技术选型依据见 `.kiro/steering/design-decisions.md`。

## 项目目标
一个桌面快捷键盘，通过 USB HID 连接 PC，模拟键盘输入触发 Kiro/VS Code 的常用操作。
3 个带屏幕的机械按键显示当前功能，1 个圆形 LCD 显示 Kiro Agent 状态，1 个矩形 LCD 显示 Agent 运行详情。

## 硬件配置
- **主控**: Waveshare ESP32-S3-DEV-KIT-N16R8 (WiFi + BLE 5, Xtensa 双核 240MHz, 16MB Flash, 8MB PSRAM)
- **按键模块**: 3x Waveshare 0.85inch ScreenKey Module B
  - 驱动: ST7735, 128x128 IPS, 65K色; 接口 SPI + 1 路按键 GPIO; GH1.25 9PIN
- **圆形LCD**: Waveshare 0.71inch LCD Module
  - 驱动: GC9D01, 160x160 IPS 圆形; 用途: 显示 Kiro Agent 状态图标
- **矩形LCD**: Waveshare 1.47inch LCD Module
  - 驱动: ST7789V3, 172x320 IPS; 用途: 显示 Agent 运行详情
- **连接方式**: 定制 PCB 板 (ESP32_5SCREEN_V0.1) 统一连接所有模块

## 通信方案：USB HID（当前）/ BLE HID（备用）
- ESP32-S3 原生 USB OTG，采用 Arduino 内置 `USB.h` + `USBHIDKeyboard.h` 模拟标准 USB 键盘
- USB 线即插即用，无需配对，延迟低、连接稳定；适合稳定触发 macOS 语音输入（双击 Control）等系统级快捷键
- BLE HID 方案（HijelHID_BLEKeyboard）代码保留在 `ble_hid.*`，如需无线可切换

## 技术栈
- **框架**: Arduino (ESP32 Arduino Core 3.x) + PlatformIO（pioarduino fork）
- **USB HID**: Arduino ESP32 内置 `USB.h` + `USBHIDKeyboard.h`（当前方案）
- **BLE HID（备用）**: HijelHID_BLEKeyboard 库 (基于 NimBLE)
- **屏幕驱动**: Arduino_GFX 库 (支持 ST7735 / GC9D01 / ST7789V3)
- **Web配置**: ESPAsyncWebServer + ArduinoJson + LittleFS
- **网络发现**: mDNS (http://kirokb.local)

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

> ⚠️ 多屏共享 SPI 总线必须手动 CS 片选（见 dev-environment.md 的硬件调试 learnings）。

## 默认按键映射

| 输入 | 功能 | 发送的快捷键 |
|------|------|-------------|
| Key1 | 打开AI聊天 | Ctrl+L |
| Key2 | 接受AI建议 | Tab |
| Key3 | 拒绝AI建议 | Escape |

## 代码模块结构 (src/)
- **config.h**: 设备名/硬件参数/消抖阈值等常量
- **pins.h**: ESP32-S3 引脚定义
- **keymap.h/.cpp**: KeyAction 动作定义 + 默认映射 + JSON 序列化 + NVS 持久化
- **keyregistry.h/.cpp**: 键名<->键码双向映射表
- **buttons.h/.cpp**: 3 个按键扫描, 消抖 + 短按/长按检测
- **ble_hid.h/.cpp**: HijelHID_BLEKeyboard 封装（备用方案）
- **display.h/.cpp**: 5 屏管理 (Arduino_GFX), 双 SPI 总线 + 独立 CS
- **webconfig.h/.cpp**: WiFi AP + ESPAsyncWebServer + REST API
- **webpage.h**: 前端配置页面 (HTML/CSS/JS 嵌入 PROGMEM)
- **main.cpp**: 主循环整合所有模块


## 串口区分 (重要)

ESP32-S3 开发板同时暴露两个 USB 串口，**用途完全不同**：

| 端口 | 用途 | 识别方式 |
|------|------|---------|
| `/dev/cu.usbmodem5B901608471` | **CH340 烧录口** (仅用于 esptool 烧录) | VID `1A86:55D3`, ioreg 显示 "USB Single Serial" |
| `/dev/cu.usbmodem14C19F35A9082` | **ESP32-S3 原生 USB CDC** (Serial 读写) | VID `303A:1001`, ioreg 显示 "ki-board" |

- **hook 脚本现在支持自动发现 CDC 口**：通过匹配 VID=0x303A、PID=0x1001 和 product 字符串 "ki-board"，脚本无需手动指定 `--serial-port` 即可找到正确端口。
- **`pio run --target upload --upload-port` 用 CH340 口** (`usbmodem5B901608471`)。
- 两个口的 usbmodem 号**拔插后可能变化**，自动发现机制消除了这个问题。
- pyserial 打开 CDC 口时必须 `dsrdtr=False, rtscts=False` 并显式 `port.dtr=False`，否则可能触发板子复位。

## Hook 集成架构

kiro-cli 的 hook (agentSpawn / userPromptSubmit / stop / postToolUse) 通过 `scripts/kiro_board_hook.py` 将事件转为 JSONL 写入 CDC 串口，板子 `pollRegistrySerial()` 在 loop 中解析并更新 agent tile 显示。

hook 脚本支持 USB 自动发现：通过 VID/PID + product 字符串 "ki-board" 匹配设备，无需在 hook 命令中硬编码串口路径。端口解析优先级为：`--serial-port` 参数 > `KIRO_BOARD_PORT` 环境变量 > 自动发现 > stdout 回退。

hook 配置在 `.kiro/agents/<agent>.json` 的 `hooks` 字段中（非全局配置）。零配置用法示例：

```bash
python3 scripts/kiro_board_hook.py --agent-name planner
```

**注意事项：**
- `agentSpawn` 仅在 agent **首次启动**时触发一次，切换回已运行的 agent 不会重新触发。因此板子重启后，需要对该 agent **发一次消息**（触发 `userPromptSubmit`）才能让 tile 显示出来。
- 多个 agent 几乎同时触发 hook 时可能串口冲突，脚本已内置 lockfile + 重试机制。
