---
inclusion: always
---

# 开发环境配置说明

## 工具链版本
- PlatformIO Core 6.1.19
- pioarduino 平台 espressif32 @ 55.3.39
- Arduino Core (framework-arduinoespressif32) @ 3.3.9
- framework-arduinoespressif32-libs @ 5.5.4
- Xtensa 工具链 toolchain-xtensa-esp-elf @ 14.2.0+20260121
- tool-xtensa-esp-elf-gdb @ 17.1.0
- esptool 5.3.0

> 注: 早期版本工具链包名为 `toolchain-xtensa-esp32s3 @ 12.2.0`，pioarduino 55.3.39
> 已改用统一的 `toolchain-xtensa-esp-elf @ 14.2.0` 并能自动安装，下方"工具链解压
> 目录嵌套问题"等手动修复步骤是早期踩坑记录，正常情况下现已不需要。

## 编译命令
PlatformIO CLI 已加入 PATH，直接用 `pio run`。
若未生效，使用完整路径：
- **Mac/Linux**: `~/.platformio/penv/bin/pio run`
- **Windows**: `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run`

Mac 上也可通过 Homebrew 安装: `brew install platformio`（当前 Mac 环境即用此方式，路径 `/opt/homebrew/bin/pio`）。

## 环境搭建踩过的坑 (重装环境时参考)

### 1. 工具链解压目录嵌套问题
pioarduino 的 toolchain-xtensa-esp32s3 包解压后可能：
- 安装到 `~/.platformio/tools/` 而非 `packages/`
- 残留 `.tmp` 后缀目录
- 编译器在 `xtensa-esp32s3-elf/bin/` 而非顶层 `bin/`
修复：把完整工具链复制到 `packages/toolchain-xtensa-esp32s3/`，确保 `bin/xtensa-esp32s3-elf-g++`（Windows 为 `.exe`）在顶层，并放入 package.json 和 .piopm 标记文件。

### 2. esptool 包残缺问题
pioarduino 的 tool-esptoolpy 带 esptool 5.3.0 的 pyproject.toml，但只有旧版单文件 esptool.py，导致 uv 安装失败。
修复：`pip install esptool==5.3.0`，再把 site-packages 中的 esptool/espefuse/espsecure/esp_rfc2217_server 四个包目录复制到 packages/tool-esptoolpy/。

### 3. 网络问题
GitHub 直连不稳定，git clone 库依赖可能超时。建议配置代理或使用镜像。

## 完整问题清单 (环境搭建踩坑复盘)

| # | 问题现象 | 根本原因 | 解决方法 |
|---|---------|---------|---------|
| 1 | `pio` 命令不识别 | CLI 未加入 PATH | Mac: `brew install platformio` 或将 `~/.platformio/penv/bin` 加入 PATH; Windows: 将 `%USERPROFILE%\.platformio\penv\Scripts` 加入用户 PATH |
| 2 | Failed to install Python dependencies | pioarduino 首次安装依赖问题 | 多与网络相关，重试 |
| 3 | UnknownPackageError: t-vk/ESP32-BLE-Keyboard | 库未发布到 PIO Registry | 改用 GitHub 源 .git URL |
| 4 | xtensa-esp32s3-elf-g++ 不是可识别命令 | 工具链装到 tools/ 而非 packages/，目录嵌套错误，残留 .tmp | 完整复制到 packages/，确保 bin/ 在顶层，补 package.json 和 .piopm |
| 5 | stdint.h/stdlib.h 找不到 | 手动复制打乱 picolibc sysroot；--force 拉来错误的12.2.0旧版 | 删除错误版本，重新拉取正确结构 |
| 6 | No module named 'esptool' | tool-esptoolpy 残缺 (有5.3.0的pyproject.toml但只有旧版单文件esptool.py) | pip install esptool==5.3.0，复制 site-packages 的4个包目录进 packages/tool-esptoolpy/ |
| 7 | esptool: unrecognized arguments --flash-mode | esptool 版本不匹配 (4.7/4.8 不接受新参数) | 安装 esptool 5.3.0 (匹配 pioarduino 55.3.39) |
| 8 | libFrameworkArduino.a 进程无法访问 | 后台编译进程未退出占用文件 | 删除 .pio/build 后重编译 |

教训：工具链用软链接（Windows junction / Mac symlink）不可靠 (PlatformIO 包管理器认不出)，直接复制实体文件最稳（Windows: robocopy, Mac: cp -R）。

## 库兼容性 (重要)
以下库与 ESP32-S3 + Arduino Core 3.x **不兼容**（原 ESP32-C6 时代发现的问题，换 S3 后仍然存在）：
- **TFT_eSPI**: 直接操作 GPIO.out_w1ts 寄存器，新芯片寄存器结构不兼容
- **ESP32-BLE-Keyboard (T-vK)**: 0.3.2 用旧版 BLE API，与 NimBLE 不兼容 (std::string/setValue)
- **ESP32Encoder (madhephaestus)**: 用旧版 PCNT 寄存器直接访问，新 IDF 驱动已重构

## 兼容方案 (社区研究结论)

### 屏幕驱动
- **Arduino_GFX** (moononournation/GFX Library for Arduino) — 已验证编译通过

### BLE HID 键盘 → HijelHID_BLEKeyboard
- 仓库: https://github.com/HijelHub/HijelHID_BLEKeyboard
- 专为 arduino-esp32 3.x + NimBLE-Arduino 构建，**支持 ESP32-S3** (仅 S2/P4 不支持)
- 依赖: NimBLE-Arduino >= 2.3.8 (在 PIO Registry: h2zero/NimBLE-Arduino)
- 已在 Win11/iOS/Android/macOS/Linux 全平台测试通过
- API: begin() / press() / release() / tap() / print()，支持 consumer/media 键(音量等)
- 含配对、电池电量、LED状态、低功耗(light/deep sleep)等完整功能
- 备选: wakwak-koba/ESP32-NimBLE-Keyboard、TheNitek/ESP32-NimBLE-Combo

### 旋转编码器 → RotaryEncoderPCNT  (⚠️ 已移除, 仅存档)
> 硬件方案已改为圆形 LCD + 矩形 LCD, 不再使用旋转编码器
> (见 design-decisions.md 第 4 节)。platformio.ini 已 `lib_ignore` 此库。
> 以下为当时的选型研究, 仅作历史存档。
- 仓库: https://github.com/vickash/RotaryEncoderPCNT (PIO library.json 已含)
- 用**新版 PCNT 驱动** (driver/pulse_cnt.h)，要求 Arduino ESP32 Core 3.0+
- **ESP32-S3 有 PCNT 单元** (仅 C2/C3 没有)，因此兼容
- 后台自动处理中断/计数溢出，API 极简: encoder.position() 返回 int32
- 备选(中断方式,任意芯片): MaffooClock/ESP32RotaryEncoder

### 注意
HijelHID 和 RotaryEncoderPCNT 可能未发布到 PIO Registry，需用 GitHub .git URL 引用。
若 git clone 超时(网络问题)，可手动下载 zip 放到 lib/ 目录。

## 当前可用库依赖 (已验证编译通过)
- bblanchon/ArduinoJson (JSON) — Registry
- moononournation/GFX Library for Arduino (屏幕) — Registry
- h2zero/NimBLE-Arduino >=2.3.8 (BLE底层) — Registry
- esp32async/ESPAsyncWebServer 3.11.1 + esp32async/AsyncTCP (Web配置) — Registry, ESP32-S3编译通过
- HijelHID_BLEKeyboard (BLE HID键盘, 当前主程序走 USB HID, 此库备用) — 手动下载到 lib/
- ~~RotaryEncoderPCNT (编码器)~~ — 已移除, platformio.ini 已 lib_ignore

注: ESPAsyncWebServer 用 esp32async/ 版本 (原mathieucarbou已迁移到ESP32Async组织)，
新版已解决旧版的 NetworkServer.h 兼容问题，无需用 mathieucarbou/ 旧名。

## 库下载方法 (绕过 git clone 超时)
GitHub git 协议在本机超时，但 codeload.github.com 可连通。下载 zip 到 lib/：

**Mac/Linux:**
```bash
curl -L "https://codeload.github.com/<owner>/<repo>/zip/refs/heads/main" -o /tmp/<repo>.zip
unzip /tmp/<repo>.zip -d <项目>/lib/
# 解压后目录名带 -main 后缀，需重命名去掉
mv <项目>/lib/<repo>-main <项目>/lib/<repo>
```

**Windows (PowerShell):**
```powershell
Invoke-WebRequest -Uri "https://codeload.github.com/<owner>/<repo>/zip/refs/heads/main" -OutFile "$env:TEMP\<repo>.zip"
Expand-Archive "$env:TEMP\<repo>.zip" -DestinationPath "<项目>\lib" -Force
# 解压后目录名带 -main 后缀，需重命名去掉
```
注意: RotaryEncoderPCNT 的 library.json 第18行有多余尾逗号 (JSON非法)，需删除。

## 硬件调试 learnings (实机验证, 2026-06)

### 多屏共享硬件 SPI 总线 → 必须手动 CS 片选 (最关键)
现象: 3 个 ScreenKey (ST7735) 共享同一硬件 SPI 总线, 若每屏各 new 一个
`Arduino_ESP32SPI` 且把 CS 交给库自动管理, 会出现**随机**花屏 / 某屏不显示 /
图像破损, 每次上电/烧录表现都不同 (典型时序竞争, 不是确定性 bug)。

根因:
- 多个 bus 对象共享同一个硬件 SPI 外设 (`_spi = &_spi_bus_array[spi_num]`)。
- 库在每次 `beginWrite/endWrite` 翻转各自的 CS, 多根共享总线 CS 交错切换。
- 每个 bus 的 `begin()` 都会 `periph_ll_reset` 复位整个 SPI 外设, 多屏 begin
  交错执行时会破坏已初始化/已绘制的其他屏。

解决 (已验证稳定):
- bus 的 CS 参数传 `GFX_NOT_DEFINED`, **不让库管 CS**。
- 自己控制 CS: 初始化和绘制某块屏时, 全程只拉低该屏 CS、其余拉高 (独占片选),
  画完再全部拉高。每块屏在"独占片选"状态下完成 `begin()` + 绘制。
- 这样同一时刻只有一块屏被选中, 后一屏的 begin() 复位不会串到前一屏。
- 注意: 各屏 DC 引脚不同 (GPIO18/8/7) 时仍需各自的 bus 对象 (DC 由 bus 持有),
  但 CS 统一手动管理。

### 双 SPI 外设隔离
- ESP32-S3 有两个可用 SPI 外设: `FSPI`(=0, SPI2) 和 `HSPI`(=1, SPI3)。
- 两条物理总线应分到不同外设 (`Arduino_ESP32SPI` 构造第 6 参 `spi_num`),
  否则它们抢同一外设、互相覆盖 SCK/MOSI 引脚 attach, 导致花屏。
- 本项目: 3×ScreenKey → HSPI (手动片选); 圆形 LCD + 矩形 LCD → FSPI。

### ST7789 1.47" (172×320) 偏移与旋转
- 列偏移 (col_offset) = 34 (172 居中于 240 宽 GRAM)。
- `rotation=1` 转为横向 320×172; 绘制坐标用 `width()/height()` 自适应更稳。

### 串口日志走原生 USB CDC, 不是 CH340
- platformio.ini 设了 `ARDUINO_USB_MODE=0` + `ARDUINO_USB_CDC_ON_BOOT=1`,
  因此 `Serial` 输出到 **ESP32-S3 原生 USB 口** (`/dev/cu.usbmodemXXXX`,
  VID `303A:1001`), 而不是 CH340 烧录口 (VID `1A86:55D3`)。
- CH340 口只用于烧录和复位 (DTR/RTS 接 EN/IO0)。
- `pio device monitor` 在非交互/管道环境会 termios 报错; 可用 pyserial 直接读。

### 烧录串口号会变
- 拔插后 `cu.usbmodem5B90160XXXX` 尾号可能变化, platformio.ini 里写死的
  `upload_port` 会失效。烧录时用 `--upload-port /dev/cu.usbmodemXXXX` 显式指定当前口,
  或先 `pio device list` 查当前 CH340 口 (VID 1A86:55D3)。

## 集成验证结果 (库全部编译通过)
当前固件 (USB HID + 5 屏 + 表情动画) 实测: RAM 17.4% (约 57KB/320KB),
Flash 46.9% (约 3.94MB/8MB, 分区表为 16MB)。资源余量充足, 已烧录实机验证通过。

> 早期纯库编译验证数据为 RAM 7.6% / Flash 23.2%, 随表情动画位图 (~3.3MB PROGMEM)
> 等内容加入后占用上升, 属正常。
