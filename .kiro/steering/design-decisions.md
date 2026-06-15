---
inclusion: always
---

# 技术选型决策记录

## 1. HID 通信方案：USB HID（当前）/ BLE HID（备用，代码保留）

### 备选方案对比

| 方案 | 优点 | 缺点 | 结论 |
|------|------|------|------|
| **A. USB HID (当前选定)** | ESP32-S3 原生 USB OTG，即插即用，延迟低，无需配对 | 有线连接 | ✓ 采用 |
| B. BLE HID | 无线使用灵活 | 首次需配对，延迟10-30ms | 代码保留, 备用 |
| C. Arduino Nano/Pro Micro 转发 | USB HID即插即用 | 多一块板子，多一根线，系统复杂 | ✗ |
| D. 外挂CH9329串口转HID | 即插即用，硬件透明 | 额外硬件成本，增加体积 | ✗ |

### 决策依据
- ESP32-S3 支持原生 USB OTG，用 Arduino 内置 `USB.h` + `USBHIDKeyboard.h` 即可模拟标准键盘
- USB HID 即插即用、延迟低、无需配对，连接稳定
- 当前场景需要稳定可靠地触发 macOS 语音输入（双击 Control）等系统级快捷键，USB HID 比 BLE 更可靠
- BLE HID 方案（HijelHID_BLEKeyboard）代码保留在 `ble_hid.*`，如需无线方案可切换

### 注意事项
- USB 模式需在 `platformio.ini` 设 `ARDUINO_USB_MODE=0`（启用原生 USB HID 而非 CDC-only）
- 普通 Arduino Nano (ATmega328P) 不支持原生 USB HID；ESP32-S3 原生支持，无需外挂转发板

## 2. 开发框架：Arduino

### 备选方案对比

| 方案 | 优点 | 缺点 | 结论 |
|------|------|------|------|
| **A. Arduino (选定)** | 开发快，库生态丰富，适合原型 | 底层控制力稍弱 | ✓ 采用 |
| B. ESP-IDF | 底层灵活，适合量产优化 | 开发周期长2-3倍，学习曲线陡 | ✗ |

### 决策依据
- 项目目标是快速做原型验证
- Arduino 生态有现成库：ESP32-BLE-Keyboard (BLE HID)、TFT_eSPI (屏幕)、ESP32Encoder (旋钮)
- 几行代码就能跑通 BLE 键盘，ESP-IDF 需要手动配置 HID Report Map
- ESP32-S3 的 Arduino Core 已经支持良好
- 后续如需量产优化，可迁移到 ESP-IDF

## 3. 屏幕驱动方案：双SPI总线 + 独立CS/DC/RST/BL

### 决策依据
- PCB (ESP32_5SCREEN_V0.1) 采用双 SPI 总线设计
- SPI-A (GPIO9/10): 3个 ScreenKey 共享 MOSI+CLK，每屏独立 CS/DC/RST/BL
- SPI-B (GPIO12/11): 圆形LCD + 矩形LCD 共享 MOSI+CLK，各自独立 CS/DC/RST/BL
- 每屏完全独立控制，可同时刷新不同屏
- Arduino_GFX 库支持 ST7735、GC9D01、ST7789V3 三种驱动芯片

## 4. 配置存储方案：NVS + Web配置

### 备选方案对比

| 方案 | 优点 | 缺点 | 结论 |
|------|------|------|------|
| **A. Web界面 (选定)** | 可视化配置，用户友好 | 需要WiFi连接 | ✓ 采用 |
| B. 串口命令行 | 实现简单 | 用户体验差 | ✗ |
| C. SD卡配置文件 | 离线可改 | 需额外硬件 | ✗ |

### 决策依据
- ESP32-S3 自带 WiFi，零成本实现 Web 配置
- 浏览器直接访问，无需安装任何 PC 端软件
- AP 模式配网 + STA 模式使用，覆盖有无路由器的场景
- mDNS 使得访问地址友好 (http://kirokb.local)
- BLE 和 WiFi 可共存，配置时不影响键盘功能
