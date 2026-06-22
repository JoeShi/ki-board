# Ki-board 项目复刻指南

> 本文档帮助你在一台全新的电脑上，从零复现 Ki-board 的完整开发过程。
> 每个阶段都标注了"你做什么"和"Kiro 做什么"，以及对应的提示词。

---

## 前置条件

| 项目 | 要求 |
|------|------|
| 电脑 | macOS (推荐) / Linux / Windows |
| Kiro IDE | 已安装并登录 |
| 浏览器 | 用于产品调研（小红书/淘宝/YouTube） |
| 预算 | 约 300 元人民币（硬件采购） |
| 基础技能 | 会用终端、会焊接或接杜邦线 |

---

## 总览：9 个阶段

```
阶段 1: 产品调研        → 了解市场上有什么
阶段 2: Idea 探讨       → 和 Kiro 对话确定方向
阶段 3: 硬件选型        → 确定买什么、从哪买
阶段 4: 原理图设计      → 确定怎么连线
阶段 5: 环境搭建        → 装好开发工具
阶段 6: 硬件验证        → 点亮第一块屏
阶段 7: 固件开发(Spec)  → 用 Spec 驱动写固件
阶段 8: Companion App   → 用 Spec 驱动写桌面应用
阶段 9: 产品化          → CI/CD + OTA + 发布
```

---


## 阶段 1：产品调研（1-2 天）

### 你做什么
打开浏览器，在以下平台搜索：

| 平台 | 搜索关键词 | 关注点 |
|------|-----------|--------|
| 小红书 | `可编程键盘 DIY`、`Stream Deck 替代`、`ESP32 键盘` | 产品形态、用户体验 |
| 淘宝 | `带屏幕按键模块`、`ESP32-S3 开发板` | 价格、供应链 |
| YouTube | `DIY macro keyboard`、`stream deck alternative` | 国外方案 |
| GitHub | `macro-keyboard`、`esp32 hid keyboard` | 开源方案 |
| B站 | `自制快捷键盘`、`ESP32 项目` | 中文教程 |

### Kiro 做什么
如果你有小红书/浏览器 Skill，可以让 Kiro 帮你搜索和整理：

```
提示词：帮我调研"面向程序员的桌面快捷控制器"这个品类。
搜索小红书、GitHub、淘宝，整理一份竞品分析表，包含：
产品名、价格、核心功能、优缺点、和我想做的东西的差异。
我想做的是一个面向 AI 编程（Kiro agentic coding）的带屏幕物理控制器。
```

### 输出物
- `docs/market-research.md`（竞品分析 + 灵感来源）

---


## 阶段 2：Idea 探讨（1 次对话，约 30 分钟）

### 你做什么
打开 Kiro，用一段粗糙的话描述你的想法。不需要想清楚，Kiro 会主动引导你。

### 启动提示词（复制粘贴到 Kiro）

```
我想做一个硬件产品。

背景：我在用 Kiro 做 agentic coding，同时开 4 个 Agent（planner/coder/reviewer/runner），
在 Ghostty 终端里用 4 个 split 管理它们。

我的痛点：
- 切换 Agent 要记快捷键，操作慢
- 不知道哪个 Agent 在忙
- 语音输入的启动/确认/取消流程太长
- 想打断 Agent 要先找到对的 split

我想做一个放桌上的物理控制器来解决这些问题。
可能带几个按键和屏幕，能看到 Agent 状态，一键切换/语音/发送/取消。

请像产品搭档一样和我讨论：
1. 先问我几个关键问题来理解需求
2. 帮我想清楚做什么和不做什么
3. 最后输出一份 Idea.md
```

### Kiro 会做什么
- 主动提问 3-4 轮（场景、形态、竞品差异、预算、目标用户）
- 总结并输出 `docs/Idea.md`

### 输出物
- `docs/Idea.md`

---


## 阶段 3：硬件选型（1 次对话 + 下单）

### 启动提示词

```
基于 docs/Idea.md，帮我做硬件选型。

我的需求：
- ESP32-S3 主控（需要 USB OTG + WiFi + BLE + 足够 GPIO）
- 3 个带小彩屏的按键模块（屏幕+按键一体）
- 1 个小圆形 LCD（显示状态图标）
- 1 个矩形 LCD（显示 Agent 总览）
- 连接线和原型板

约束：
- 总预算 300 元以内
- 优先选微雪(Waveshare)模块（文档全、接口统一）
- 在淘宝能买到现货
- 我会焊接，但优先选即插即用的模块

请帮我：
1. 列出每个模块的 2-3 个候选方案
2. 对比规格、价格、兼容性
3. 给出推荐组合和理由
4. 计算总成本
5. 输出 BOM 采购清单
6. 提醒我需要注意的兼容性风险
```

### 输出物
- `docs/hardware-selection.md`（BOM + 选型对比 + 风险）

### 你的动作
- 在淘宝下单购买所有模块
- 等待发货（通常 2-3 天）

---


## 阶段 4：原理图与接线设计（硬件到货前）

### 启动提示词

```
基于 docs/hardware-selection.md 的 BOM，帮我设计接线方案。

硬件清单：
- Waveshare ESP32-S3-DEV-KIT-N16R8
- 3x Waveshare 0.85inch ScreenKey Module B (ST7735, SPI, 128x128)
- 1x Waveshare 0.71inch LCD Module (GC9D01, 160x160, 圆形)
- 1x Waveshare 1.47inch LCD Module (ST7789V3, 172x320)

设计要求：
1. 5 个屏幕分成两组 SPI 总线（3个ScreenKey共享SPI-A，圆形+矩形共享SPI-B）
2. 每屏独立 CS/DC/RST/BL 引脚
3. 3 个按键各用一个 GPIO（内部上拉）
4. USB 保留给 HID + CDC
5. 预留 BLE 天线区域（不被走线干扰）

请输出：
1. 完整的 GPIO 分配表（pins.h 格式）
2. 接线图（文字描述，按模块分组）
3. SPI 总线分组说明
4. 面包板接线注意事项
5. 后续 PCB 设计时的布局建议
```

### 输出物
- `include/pins.h`
- `docs/硬件调试指南.md`（接线表部分）

---


## 阶段 5：开发环境搭建（硬件到货前）

### 启动提示词

```
帮我搭建 Ki-board 项目的开发环境。

目标：
- PlatformIO + Arduino 框架
- ESP32-S3 目标板
- 能编译通过（暂时不需要烧录）

请帮我：
1. 创建 platformio.ini 配置文件
   - 平台用 pioarduino fork
   - board: esp32-s3-devkitc1-n16r8
   - 16MB Flash, OPI PSRAM
   - USB HID 模式 (ARDUINO_USB_MODE=0, ARDUINO_USB_CDC_ON_BOOT=1)
   - USB_PRODUCT 设为 "ki-board"
2. 创建项目目录结构 (src/, include/, lib/, docs/, scripts/, test/)
3. 添加库依赖：Arduino_GFX, ArduinoJson, NimBLE-Arduino, ESPAsyncWebServer
4. 写一个最小的 main.cpp（Serial.println hello world）
5. 确认 pio run 能编译通过
6. 记录搭建过程中遇到的坑到 .kiro/steering/dev-environment.md
```

### 你的动作
```bash
# 安装 PlatformIO
# Mac:
brew install platformio
# 或:
pip install platformio

# 验证
pio run
```

### 输出物
- `platformio.ini`
- 项目目录结构
- `.kiro/steering/dev-environment.md`（踩坑记录）

---


## 阶段 6：硬件验证（硬件到货后，1-2 天）

### 你做什么
按面包板接线图连接硬件，然后逐步验证。

### 启动提示词

```
硬件到货了，帮我写一个分阶段验证程序。

验证顺序：
A. 主控上电 - USB 连接后串口有输出
B. 单屏点亮 - 先只接 1 个 ScreenKey，显示彩色方块
C. 多屏点亮 - 接全部 3 个 ScreenKey，验证 SPI 共享无花屏
D. 圆屏点亮 - 接 GC9D01 圆形 LCD，显示 Kiro ghost 图标
E. 矩形屏点亮 - 接 ST7789 矩形 LCD，显示文字
F. 按键读取 - 按每个键串口打印
G. USB HID - 电脑识别为键盘，按键发送字符

每一步写一个独立的测试程序（放在 test/ 目录），
platformio.ini 里配置独立的 env 来编译每个测试。

重点关注：
- 多屏共享 SPI 的 CS 片选问题（必须手动管 CS）
- ST7735 的行列偏移参数
- 圆屏 GC9D01 和 ST7789 的初始化参数
```

### 输出物
- `test/test_single_screen.cpp`
- `test/test_round_lcd.cpp`
- `test/test_all_screens.cpp`
- `test/test_buttons.cpp`
- `test/test_usb_hid.cpp`
- 更新 `docs/硬件调试指南.md`（调试经验）
- 更新 `.kiro/steering/dev-environment.md`（硬件调试 learnings）

---


## 阶段 7：固件开发 — Spec 驱动（5-7 天）

硬件验证通过后，进入正式固件开发。按 6 个 Spec 顺序推进：

### Spec 7.1：硬件驱动层

```
帮我创建一个 Spec，实现 Ki-board 项目的硬件驱动层模块。

背景：
- ESP32-S3 + 5 屏幕桌面控制器，硬件验证已通过
- 3 个 ST7735 ScreenKey 共享 SPI-A，1 个 GC9D01 + 1 个 ST7789 共享 SPI-B

需要实现：
1. display_hardware 模块：双 SPI 总线 5 屏初始化，手动 CS 片选，逻辑键到物理屏映射（含焊接交换支持）
2. hid_actions 模块：USB HID 键盘输出封装（Cmd+], 双击Control, Enter, ESC, Backspace）
3. 按键 GPIO 初始化

技术约束：Arduino + PlatformIO, Arduino_GFX 库, ESP32 USB.h + USBHIDKeyboard.h
多屏共享 SPI 必须手动 CS 片选解决时序竞争。

参考：#[[file:include/pins.h]] #[[file:.kiro/steering/dev-environment.md]]
```

### Spec 7.2：UI 渲染层

```
帮我创建一个 Spec，实现 Ki-board 项目的 UI 渲染层。

背景：硬件驱动层已就绪，5 块屏幕可独立绘制。

需要实现：
1. ScreenKey 图标：麦克风、对勾、ESC、退格、切换Agent
2. 圆形 LCD 表情动画：idle/work/wait 三组帧序列，按 FPS 播放
3. 矩形 LCD 四象限：4 个 Agent 名称+状态，选中高亮，录音显示 REC，编辑显示 EDIT
4. 特殊界面：OTA 进度、配对码显示

约束：绘制函数接收 Arduino_GFX*，表情帧 PROGMEM 位图约 3.3MB，矩形屏横向 320x172。

参考：#[[file:docs/kiro-keyboard-tech-design.md]] 第6节
```

### Spec 7.3：状态机与按键逻辑

```
帮我创建一个 Spec，实现核心状态机和按键交互。

背景：驱动和 UI 已就绪，现在实现按键→状态→HID→UI 的完整循环。

需要实现：
1. 按键消抖 + 短按/长按检测
2. 三态状态机：普通 ↔ VoiceRecording ↔ VoiceEditing
3. 按键行为矩阵（普通/录音/编辑三种模式下左中右键的不同行为）
4. Agent 切换（循环切换，录音态禁止）
5. 状态乐观更新（发送→Running，ESC→Idle）
6. 按键事件 JSONL 上报
7. 状态变化后刷新全部 UI

约束：voice_engine 区分 system/third_party 两种模式。

参考：#[[file:docs/kiro-keyboard-tech-design.md]] 第4/5/7节
      #[[file:docs/kiro-keyboard-prd.md]] 第4/5/7节
```

### Spec 7.4：Agent 注册与 Hook 同步

```
帮我创建一个 Spec，实现 Agent 注册表和 Kiro CLI Hook 状态同步。

背景：状态机已就绪，现在让板子接收 Kiro CLI 的真实 Agent 状态。

需要实现：
1. agent_registry：4 个 AgentSlot，JSONL 串口解析，注册/更新/替换规则
2. hook 脚本 (Python)：读 STDIN Kiro hook JSON，USB 自动发现写入 CDC 串口
3. companion 心跳 ping/pong

约束：JSONL 协议，ArduinoJson 解析，pyserial 自动发现(VID/PID/product)。

参考：#[[file:docs/kiro-agent-status-sync.md]]
```

### Spec 7.5：BLE 通信 + 配对 + OTA

```
帮我创建一个 Spec，实现 BLE GATT 通信、安全配对和 OTA 升级。

需要实现：
1. BLE GATT：自定义 service，RX/TX characteristic，与 USB CDC 共用 JSONL 协议
2. 配对：6位随机码，板子确认，token 存 NVS，后续连接 auth 验证
3. OTA：ota_begin/chunk/end 协议，双分区，进度显示，失败回滚

约束：NimBLE-Arduino >= 2.3.8，chunk 512字节，SHA256 校验。
```

### 里程碑验证

Spec 7.3 完成后即可烧录做第一次端到端验证：
```bash
pio run -e esp32-s3 --target upload --upload-port /dev/cu.usbmodemXXXX
```

---


## 阶段 8：Companion App — Spec 驱动（5-7 天）

### 启动提示词

```
帮我创建一个 Spec，实现 Ki-board 的 Companion 桌面应用。

这是一个 macOS 桌面应用，作为板子和 Kiro 生态之间的桥梁。

技术栈：Tauri 2.x (Rust 后端 + React/Vite/TypeScript 前端)

需要实现：
1. Transport 层：USB CDC 自动发现 + BLE GATT，统一 BoardTransport trait
2. Hook 监听：TCP 127.0.0.1:47218 接收 Kiro hook 事件，转发给板子
3. 语音 ASR：cpal 录音 + WebSocket 流式 Doubao ASR + 粘贴/发送
4. 按键事件：接收 button_event，根据 voice_intent 驱动录音
5. 设置：~/.ki-board/settings.json + keyring API key
6. OTA：读 .bin 文件通过 transport 刷写
7. 前端 UI：状态面板、设置页、日志、OTA 页

参考：#[[file:companion/src-tauri/Cargo.toml]]
```

### 输出物
- `companion/` 完整目录结构
- Tauri 后端 + React 前端

---

## 阶段 9：产品化（1-2 天）

### 启动提示词

```
帮我为 Ki-board 项目配置 CI/CD 和版本管理。

需要实现：
1. GitHub Actions workflow：push to main 自动触发
   - 自动 bump 版本号
   - 编译固件（pio run 两个 env）
   - 编译 Companion App（Tauri universal binary for macOS）
   - 创建 GitHub Release + 上传 artifacts
2. 版本管理脚本：
   - scripts/app_version.py（companion 版本）
   - scripts/firmware_version.py（固件版本）
   - scripts/auto_version.py（编译时注入版本号到固件）
3. Release 产物命名规范：
   - kiro-board-firmware-v{version}-fw{fw_version}.bin
   - ki-board-v{version}-universal.dmg

参考现有的 .github/workflows/release.yml 结构。
```

### 输出物
- `.github/workflows/release.yml`
- `scripts/app_version.py`
- `scripts/firmware_version.py`
- `scripts/auto_version.py`

---


## 时间线总结

| 阶段 | 耗时 | 阻塞项 | 可并行 |
|------|------|--------|--------|
| 1. 产品调研 | 1-2 天 | 无 | - |
| 2. Idea 探讨 | 30 分钟 | 无 | - |
| 3. 硬件选型 | 1 小时 + 等发货 | 无 | - |
| 4. 原理图设计 | 2 小时 | 无 | 与阶段 5 并行 |
| 5. 环境搭建 | 2-4 小时 | 无 | 与阶段 4 并行 |
| 6. 硬件验证 | 1-2 天 | 硬件到货 | - |
| 7. 固件开发 | 5-7 天 | 阶段 6 通过 | - |
| 8. Companion App | 5-7 天 | Spec 7.4 完成 | 前端可提前 |
| 9. 产品化 | 1-2 天 | 阶段 7+8 完成 | - |

**总计：约 2-3 周**（含等硬件发货时间）

---

## Kiro Steering 配置（项目开始时创建）

在项目根目录创建以下文件，让 Kiro 理解项目上下文：

### `.kiro/steering/communication.md`
```markdown
---
inclusion: always
---
# 沟通语言
- 始终使用中文与用户沟通
- 代码注释可以使用英文，但解释和讨论使用中文
```

### `.kiro/steering/design-decisions.md`
```markdown
---
inclusion: always
---
# 技术选型决策记录
（随着阶段推进逐步填充）
```

### `.kiro/steering/dev-environment.md`
```markdown
---
inclusion: always
---
# 开发环境配置说明
（搭建环境时记录踩坑经验）
```

---

## 关键经验提醒

1. **先验证风险最高的**：多屏 SPI 共享是最大硬件风险，阶段 6 优先验证
2. **文档先行**：每个阶段的输出物先是文档，再是代码
3. **Spec 按依赖顺序**：7.1→7.2→7.3 必须按顺序，7.4/7.5 可以并行
4. **编译验证频繁**：每个 Spec 完成一个 task 就 `pio run` 验证
5. **保留备用方案代码**：BLE HID、Web 配置等模块写了不删，用 build_src_filter 控制
6. **乐观更新 + 外部校准**：UI 立即响应按键，hook 事件后续校准最终状态
