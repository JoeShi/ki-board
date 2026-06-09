# Kiro 快捷键盘 (Vibe Coding Keyboard)

一个基于 ESP32-C6 的桌面快捷键盘，通过 BLE 蓝牙连接电脑，模拟键盘输入来触发 Kiro / VS Code 的常用操作。4 个带彩屏的机械按键显示当前功能，1 个旋转编码器提供滚动、撤销、切换标签等连续控制，所有按键行为都可以通过浏览器网页自定义。

> 状态：软件开发完成并编译通过，等待硬件到货后烧录验证。

---

## ✨ 功能特性

- **4 个屏幕按键**：每个按键带 0.85 寸彩屏，显示当前绑定的功能
- **旋转编码器**：带按压，支持 3 种工作模式（滚动 / 撤销重做 / 切换标签）循环切换
- **BLE HID 键盘**：无线连接，无需 PC 端驱动软件，即配即用
- **Web 可视化配置**：手机/电脑连上设备热点，浏览器即可自定义所有按键映射
- **配置持久化**：配置存入 NVS，断电重启不丢失

---

## 🧩 硬件配置

| 部件 | 型号 | 数量 |
|------|------|------|
| 主控 | ESP32-C6-DEV-KIT-N16 (WiFi6 + BLE5, 16MB Flash) | 1 |
| 屏幕按键 | Waveshare 0.85inch ScreenKey Module B (ST7735, 128×128) | 4 |
| 旋转编码器 | EC11 (带按压) | 1 |
| 连接 | 杜邦线 + 面包板 / PCB 万用板 | - |

详细接线见 [docs/硬件调试指南.md](docs/硬件调试指南.md)。

---

## 🎹 默认按键映射

| 输入 | 功能 | 快捷键 |
|------|------|--------|
| Key 1 | 打开 AI 聊天 | `Ctrl+L` |
| Key 2 | 接受 AI 建议 | `Tab` |
| Key 3 | 拒绝 AI 建议 | `Escape` |
| Key 4 | 命令面板 | `Ctrl+Shift+P` |
| 旋钮短按 | 切换旋钮模式 | — |
| 旋钮长按 | 保存文件 | `Ctrl+S` |
| 旋钮旋转 (模式1) | 上下滚动 | `↑` / `↓` |
| 旋钮旋转 (模式2) | 撤销 / 重做 | `Ctrl+Z` / `Ctrl+Y` |
| 旋钮旋转 (模式3) | 切换标签页 | `Ctrl+PgUp` / `Ctrl+PgDn` |

所有映射均可通过 Web 界面修改。

---

## 🛠 技术栈

- **框架**：Arduino (ESP32 Arduino Core 3.x) + PlatformIO
- **平台**：pioarduino fork (官方平台暂不支持 ESP32-C6 + Arduino 3.x)
- **BLE HID**：[HijelHID_BLEKeyboard](https://github.com/HijelHub/HijelHID_BLEKeyboard) (基于 NimBLE)
- **屏幕驱动**：[Arduino_GFX](https://github.com/moononournation/Arduino_GFX)
- **旋转编码器**：[RotaryEncoderPCNT](https://github.com/vickash/RotaryEncoderPCNT) (硬件 PCNT)
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
│   ├── encoder_input.*     # 旋钮封装 (方向 + 模式切换)
│   ├── ble_hid.*           # BLE HID 键盘封装
│   ├── display.*           # 4 屏 ST7735 管理
│   ├── webconfig.*         # WiFi AP + Web 服务器 + REST API
│   └── webpage.h           # 前端配置页面 (PROGMEM)
├── lib/                    # 手动下载的库 (随仓库提交)
│   ├── HijelHID_BLEKeyboard/
│   └── RotaryEncoderPCNT/
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
1. 烧录后设备启动，电脑蓝牙搜索并配对 **"Kiro KB"**
2. 按键 / 旋钮即可触发对应快捷键
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

- **库兼容性**：ESP32-C6 + Arduino Core 3.x 生态较新，TFT_eSPI、老版 BLE/编码器库均不兼容，已选用兼容替代品（详见开发环境笔记）。
- **lib/ 目录随仓库提交**：HijelHID_BLEKeyboard 和 RotaryEncoderPCNT 因 GitHub clone 易超时，已放入 `lib/` 直接提交，确保开箱即用。
- **安全提示**：当前 WiFi 配置热点为开放无密码，配置接口无认证，仅适合可信环境的原型阶段使用。

---

## 🗺 开发进度

- [x] 开发环境搭建 (PlatformIO + pioarduino + ESP32-C6)
- [x] 屏幕显示模块 (4× ST7735)
- [x] 按键 + 旋钮输入
- [x] BLE HID 键盘
- [x] Web 配置系统 (WiFi AP + REST API + NVS)
- [ ] 硬件到货后烧录验证
- [ ] 外壳设计 / PCB (可选)

---

## 📄 许可

本项目代码供学习与个人使用。所用第三方库各自遵循其原始许可证。
