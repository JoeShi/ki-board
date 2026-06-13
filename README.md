# Kiro 快捷键盘 (Vibe Coding Keyboard)

一个基于 ESP32-S3 的桌面快捷键盘，通过 USB HID 连接电脑，模拟键盘输入来触发 Kiro / VS Code 的常用操作。3 个带彩屏的机械按键显示当前功能，1 个圆形 LCD 显示 Kiro Agent 状态，1 个矩形 LCD 显示 Agent 运行详情，所有按键行为都可以通过浏览器网页自定义。

> 状态：软件开发完成并编译通过，等待硬件到货后烧录验证。

---

## ✨ 功能特性

- **3 个屏幕按键**：每个按键带 0.85 寸彩屏，显示当前绑定的功能
- **圆形状态 LCD**：0.71 寸圆形屏显示 Kiro Agent 运行状态图标
- **矩形信息 LCD**：1.47 寸矩形屏显示 Agent 详细运行信息
- **USB HID 键盘**：USB 线即插即用，无需 PC 端驱动软件
- **Web 可视化配置**：手机/电脑连上设备热点，浏览器即可自定义所有按键映射
- **配置持久化**：配置存入 NVS，断电重启不丢失

---

## 🧩 硬件配置

| 部件 | 型号 | 数量 |
|------|------|------|
| 主控 | Waveshare ESP32-S3-DEV-KIT-N16R8 (WiFi + BLE5, Xtensa 双核 240MHz, 16MB Flash, 8MB PSRAM) | 1 |
| 屏幕按键 | Waveshare 0.85inch ScreenKey Module B (ST7735, 128×128) | 3 |
| 圆形LCD | Waveshare 0.71inch LCD Module (GC9D01, 160×160, 圆形) | 1 |
| 矩形LCD | Waveshare 1.47inch LCD Module (ST7789V3, 172×320) | 1 |
| 连接 | 定制PCB板 (ESP32_5SCREEN_V0.1) | 1 |

详细接线见 [docs/硬件调试指南.md](docs/硬件调试指南.md)。

---

## 🎹 默认按键映射

| 输入 | 功能 | 快捷键 |
|------|------|--------|
| Key 1 | 打开 AI 聊天 | `Ctrl+L` |
| Key 2 | 接受 AI 建议 | `Tab` |
| Key 3 | 拒绝 AI 建议 | `Escape` |

所有映射均可通过 Web 界面修改。

---

## 🛠 技术栈

- **框架**：Arduino (ESP32 Arduino Core 3.x) + PlatformIO
- **平台**：pioarduino fork (官方平台暂不支持 ESP32-S3 + Arduino 3.x 完整组合)
- **USB HID**：Arduino ESP32 内置 `USB.h` + `USBHIDKeyboard.h`（当前连接方式）
- **BLE HID（备用，代码保留）**：[HijelHID_BLEKeyboard](https://github.com/HijelHub/HijelHID_BLEKeyboard) (基于 NimBLE)
- **屏幕驱动**：[Arduino_GFX](https://github.com/moononournation/Arduino_GFX)
- **Web 服务**：[ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer) + ArduinoJson

选型理由详见 [.kiro/steering/design-decisions.md](.kiro/steering/design-decisions.md)。

---

## 📁 项目结构

```
vibe-coding-keyboard/
├── platformio.ini          # PlatformIO 工程配置
├── include/                # 全局头文件 (引脚/常量)
│   ├── pins.h
│   ├── config.h
│   └── User_Setup.h
├── src/                    # 源代码
│   ├── main.cpp            # 主循环整合
│   ├── keymap.*            # 按键动作定义 + JSON + NVS 持久化
│   ├── keyregistry.*       # 键名↔键码映射表
│   ├── buttons.*           # 按键扫描 (消抖 + 长短按)
│   ├── ble_hid.*           # BLE HID 键盘封装 (备用方案, 当前主程序走 USB HID)
│   ├── display.*           # 5屏管理 (3×ST7735 + GC9D01 + ST7789V3)
│   ├── webconfig.*         # WiFi AP + Web 服务器 + REST API
│   └── webpage.h           # 前端配置页面 (PROGMEM)
├── lib/                    # 手动下载的库 (随仓库提交)
│   └── HijelHID_BLEKeyboard/
├── partitions/             # 16MB Flash 分区表
└── docs/                   # 文档
    ├── 硬件调试指南.md
    └── 技术概念百科.md
```

---

## 🚀 快速开始

### 环境要求
- [PlatformIO IDE](https://platformio.org/) 扩展 (VS Code / Kiro)
- 首次编译会自动下载 pioarduino 平台、工具链和库依赖

### 编译与烧录

```bash
# 编译
pio run

# 编译 + 烧录到开发板
pio run --target upload

# 查看串口日志
pio device monitor --baud 115200
```

> 若 `pio` 命令不可用，使用完整路径：
> `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run`

### 使用
1. 烧录后用 USB 线连接电脑，设备自动识别为键盘 **"Kiro KB"**，无需配对
2. 按键即可触发对应快捷键
3. 自定义配置：手机/电脑连接 WiFi 热点 **"KiroKeyboard-Config"**，浏览器打开 `http://192.168.4.1`

---

## 📖 文档

| 文档 | 内容 |
|------|------|
| [硬件调试指南](docs/硬件调试指南.md) | 接线表、烧录、分阶段验证、参数微调、故障排查 |
| [技术概念百科](docs/技术概念百科.md) | 面向小白的全部技术概念通俗讲解 |
| [项目总览](.kiro/steering/project-overview.md) | 硬件配置、引脚分配、开发阶段、Web 配置方案 |
| [技术选型决策](.kiro/steering/design-decisions.md) | 各项方案的对比分析与决策依据 |
| [开发环境笔记](.kiro/steering/dev-environment.md) | 环境搭建踩坑记录、库兼容性、工具链问题 |

---

## ⚠️ 注意事项

- **库兼容性**：ESP32-S3 + Arduino Core 3.x 生态下，TFT_eSPI、老版 BLE/编码器库均不兼容，已选用兼容替代品（详见开发环境笔记）。
- **lib/ 目录随仓库提交**：HijelHID_BLEKeyboard 因 GitHub clone 易超时，已放入 `lib/` 直接提交，确保开箱即用。
- **安全提示**：当前 WiFi 配置热点为开放无密码，配置接口无认证，仅适合可信环境的原型阶段使用。

---

## 🗺 开发进度

- [x] 开发环境搭建 (PlatformIO + pioarduino + ESP32-S3)
- [x] 屏幕显示模块 (4× ST7735)
- [x] 按键 + 旋钮输入
- [x] USB HID 键盘 (当前方案) / BLE HID (备用, 代码保留)
- [x] Web 配置系统 (WiFi AP + REST API + NVS)
- [ ] 硬件到货后烧录验证
- [ ] 外壳设计 / PCB (可选) / PCB (可选)

---

## 📄 许可

本项目代码供学习与个人使用。所用第三方库各自遵循其原始许可证。
