# 需求文档

## 简介

为 Kiro 键盘（基于 ESP32-S3 的 5 屏桌面控制器）添加 WiFi OTA（空中固件升级）功能。用户通过同时长按三个按键 10 秒进入 WiFi AP 模式，设备启动热点并提供 Web 界面，用户可从手机或电脑连接热点并上传 .bin 固件文件完成升级。矩形 LCD 在整个过程中显示 OTA 状态与进度。

## 术语表

- **WiFi_OTA_Manager**：负责 WiFi OTA 模式生命周期管理的固件模块，包括进入/退出 OTA 模式、HTTP 文件上传处理、固件校验与写入
- **Button_Detector**：按键检测模块，负责检测三键同时长按手势并触发模式切换
- **OTA_Web_Server**：在 WiFi AP 模式下运行的 HTTP 服务器，提供固件上传 Web 界面和文件接收 API
- **Rect_Display**：1.47 英寸矩形 LCD（ST7789V3, 172x320），用于显示 OTA 状态和进度信息
- **Round_Display**：1.28 英寸圆形 LCD（GC9A01, 240x240），用于显示 OTA 标识和进度百分比
- **WiFi_AP**：设备启动的 WiFi 接入点，供用户设备连接以完成固件上传
- **Firmware_Image**：用户上传的 .bin 格式固件二进制文件
- **OTA_Partition**：Flash 中用于存储新固件的 OTA 分区（app0 或 app1，每个 7MB）

## 需求

### 需求 1：三键长按触发 OTA 模式

**用户故事：** 作为键盘用户，我希望通过同时长按三个按键 10 秒来进入 WiFi OTA 模式，以便在不拆机、不连接数据线的情况下升级固件。

#### 验收标准

1. WHEN 三个 ScreenKey 按键（KEY1、KEY2、KEY3）均处于按下状态且最早被按下的按键持续按住时间达到 10000 毫秒, THE Button_Detector SHALL 触发系统进入 WiFi OTA 模式并启动 WiFi AP 热点以接受固件升级连接
2. IF 任意一个 ScreenKey 按键在三键同时按下持续时间达到 10000 毫秒之前被释放, THEN THE Button_Detector SHALL 取消 OTA 触发计时并保持当前正常运行模式不变
3. WHILE WiFi OTA 模式处于激活状态, THE Button_Detector SHALL 忽略所有按键输入的常规功能（HID 输出、手势识别），直到设备重启
4. IF 三键长按在 5000 毫秒时已触发 WiFi 凭据清除逻辑, THEN THE Button_Detector SHALL 继续计时，并在三键持续按住累计达到 10000 毫秒时仍然进入 WiFi OTA 模式
5. WHILE 设备处于 BLE 配对流程中（PAIRING_PAIRING 阶段）, THE Button_Detector SHALL 不响应三键长按 OTA 触发手势，按键事件按正常配对流程处理
6. WHEN 系统成功进入 WiFi OTA 模式时, THE Button_Detector SHALL 在矩形 LCD 屏幕上显示 OTA 等待状态指示，告知用户设备已准备好接收固件

### 需求 2：WiFi AP 模式启动

**用户故事：** 作为键盘用户，我希望设备在 OTA 模式下自动启动 WiFi 热点，以便我从手机或电脑连接到设备进行固件升级。

#### 验收标准

1. WHEN WiFi OTA 模式被触发, THE WiFi_OTA_Manager SHALL 在 3 秒内启动一个 WiFi 接入点，SSID 格式为 "KiroKB-XXXXXX"（XXXXXX 为设备 MAC 地址后三字节的十六进制大写表示，共 6 个字符），最大允许 1 个客户端同时连接
2. WHEN WiFi OTA 模式被触发, THE WiFi_OTA_Manager SHALL 设置 AP 密码为 "kiro-XXXXXX"（XXXXXX 与 SSID 后缀一致），使用 WPA2 认证模式
3. WHEN WiFi AP 启动完成, THE OTA_Web_Server SHALL 在端口 80 上开始监听 HTTP 请求，并在 Rect_Display 上显示 AP 的 SSID、密码和 IP 地址（192.168.4.1）以便用户连接
4. IF WiFi AP 在触发后 3 秒内未成功启动, THEN THE WiFi_OTA_Manager SHALL 在 Rect_Display 上显示包含失败原因的错误信息，并在 5 秒后退出 OTA 模式、恢复之前的 WiFi 工作模式（STA 或 AP）和正常键盘运行
5. WHEN WiFi OTA 模式激活, THE WiFi_OTA_Manager SHALL 先停止已有的 Web 配置服务（webconfig）并断开当前所有 WiFi STA 连接，再切换 WiFi 为纯 AP 模式启动 OTA 接入点
6. IF WiFi OTA 模式持续 300 秒无客户端连接或无 HTTP 请求活动, THEN THE WiFi_OTA_Manager SHALL 自动退出 OTA 模式并恢复之前的 WiFi 工作模式和正常键盘运行

### 需求 3：OTA Web 界面

**用户故事：** 作为键盘用户，我希望通过浏览器访问一个简洁的 Web 页面来上传固件文件，以便完成 OTA 升级操作。

#### 验收标准

1. WHEN 用户通过浏览器访问 WiFi AP 的 IP 地址（默认 192.168.4.1）的 OTA 路径, THE OTA_Web_Server SHALL 在 3 秒内返回一个包含固件上传表单的 HTML 页面
2. THE OTA_Web_Server SHALL 在上传页面中以 "MAJOR.MINOR.PATCH" 格式显示当前固件版本号
3. THE OTA_Web_Server SHALL 在上传页面中提供一个文件选择控件，仅接受 .bin 扩展名的文件
4. WHEN 用户选择 .bin 文件并点击上传按钮, THE OTA_Web_Server SHALL 通过 HTTP multipart/form-data POST 请求接收 Firmware_Image 并在页面上显示上传进度（已传输百分比）
5. IF 用户上传的文件大小超过 OTA_Partition 容量（7,340,032 字节 / 7MB）, THEN THE OTA_Web_Server SHALL 拒绝上传并在页面上显示包含文件大小上限的错误提示，且不写入 OTA 分区
6. IF 用户上传的文件大小为 0 字节, THEN THE OTA_Web_Server SHALL 拒绝上传并在页面上显示文件无效的错误提示，且不写入 OTA 分区
7. WHEN OTA_Web_Server 成功接收并写入完整的 Firmware_Image, THE OTA_Web_Server SHALL 在页面上显示升级成功提示，并在 3 秒内自动重启设备以加载新固件
8. IF 固件写入过程中发生错误（写入失败或校验失败）, THEN THE OTA_Web_Server SHALL 中止写入、回滚至原有固件分区，并在页面上显示包含失败原因的错误提示
9. WHILE OTA 上传正在进行中, THE OTA_Web_Server SHALL 拒绝新的上传请求并返回提示表明设备正忙

### 需求 4：固件写入与校验

**用户故事：** 作为键盘用户，我希望上传的固件能被正确写入并经过校验，以确保升级后设备能正常工作。

#### 验收标准

1. WHEN WiFi_OTA_Manager 接收到 OTA 写入请求且固件大小不超过 7MB（OTA 分区容量）, THE WiFi_OTA_Manager SHALL 调用 Arduino Update 库开始将固件数据流式写入下一个可用的 OTA_Partition，并以每接收一个数据块（最大 512 字节）为单位执行写入
2. WHILE 固件数据写入进行中, THE WiFi_OTA_Manager SHALL 对每个接收到的数据块累积计算 SHA-256 哈希值
3. WHEN 全部固件数据写入完成且已接收字节数等于预期大小, THE WiFi_OTA_Manager SHALL 将累积计算的 SHA-256 哈希值与请求中提供的预期 SHA-256 值进行比对，以验证写入数据的完整性
4. IF 固件 SHA-256 校验失败, THEN THE WiFi_OTA_Manager SHALL 中止 OTA 操作、保留当前运行分区不变、在 Rect_Display 上显示校验失败信息、并在 5 秒后退出 OTA 模式
5. IF 固件写入过程中连续 15 秒未收到新的数据块, THEN THE WiFi_OTA_Manager SHALL 中止 OTA 操作并将状态重置为超时
6. WHEN 固件 SHA-256 校验通过, THE WiFi_OTA_Manager SHALL 调用 Update.end(true) 将新写入的分区设置为下次启动分区
7. WHEN 新启动分区设置成功, THE WiFi_OTA_Manager SHALL 在 Rect_Display 上显示升级成功信息并在 900 毫秒后自动重启设备

### 需求 5：矩形 LCD 状态显示

**用户故事：** 作为键盘用户，我希望在 OTA 过程中矩形 LCD 能实时显示当前状态和进度，以便我了解升级是否正常进行。

#### 验收标准

1. WHEN WiFi OTA 模式被触发, THE Rect_Display SHALL 在 1 秒内显示 "Entering OTA..." 提示信息，并清除之前的四象限布局内容
2. WHEN WiFi AP 启动完成, THE Rect_Display SHALL 显示 AP 的 SSID（最长 32 字符）、密码（最长 64 字符）和 IP 地址，三项信息同时可见于屏幕上
3. WHILE 等待用户连接和上传固件, THE Rect_Display SHALL 显示 "Waiting..." 状态；IF 等待时间超过 300 秒且无客户端连接, THEN THE Rect_Display SHALL 显示超时错误信息
4. WHILE 固件上传和写入进行中, THE Rect_Display SHALL 以进度条形式显示当前写入进度百分比（0–100%），进度条每接收一个数据块后刷新
5. WHEN 固件校验通过, THE Rect_Display SHALL 显示 "Update OK, rebooting" 信息并保持显示至少 800 毫秒，随后设备自动重启
6. IF 任何阶段发生错误, THEN THE Rect_Display SHALL 显示对应错误类别信息（包括但不限于：校验失败、固件超出分区大小、写入失败、传输超时），错误信息持续显示直到设备被手动重启或下一次 OTA 流程启动
7. WHILE WiFi OTA 模式处于活跃状态, THE Round_Display SHALL 显示 "OTA" 标识和当前进度百分比数值，与 Rect_Display 进度保持同步

### 需求 6：超时与安全退出

**用户故事：** 作为键盘用户，我希望如果 OTA 模式长时间无操作能自动退出，以避免设备一直停留在 OTA 状态无法正常使用。

#### 验收标准

1. IF WiFi OTA 模式激活后 300 秒内无客户端连接到 WiFi AP, THEN THE WiFi_OTA_Manager SHALL 自动退出 OTA 模式、丢弃所有 OTA 临时状态、并调用 ESP.restart() 重启设备使其恢复为标准 BLE HID 键盘模式
2. IF 客户端已连接但 300 秒内未收到 ota_begin 命令, THEN THE WiFi_OTA_Manager SHALL 自动退出 OTA 模式、丢弃所有 OTA 临时状态、并调用 ESP.restart() 重启设备使其恢复为标准 BLE HID 键盘模式
3. IF 固件上传开始（收到第一个数据块）后 180 秒内未完成传输, THEN THE WiFi_OTA_Manager SHALL 中止当前上传、回滚已写入的分区数据使其不被标记为启动分区、在 Rect_Display 上显示表示传输超时且设备即将重启的提示信息、并在 5 秒后调用 ESP.restart() 重启设备
4. WHILE 任一超时等待阶段（无客户端连接等待、无上传开始等待、上传进行中等待）进行中, THE Rect_Display SHALL 以不超过每 1 秒一次的频率刷新并显示当前阶段的剩余等待秒数
5. IF 固件上传超时导致中止, THEN THE WiFi_OTA_Manager SHALL 确保未完成的固件镜像不被设置为下次启动分区，设备重启后从当前有效固件正常启动

### 需求 7：与现有功能的共存

**用户故事：** 作为键盘用户，我希望 WiFi OTA 功能不影响现有的按键操作、BLE 通讯和 USB HID 功能的正常工作。

#### 验收标准

1. WHILE WiFi OTA 模式未激活, THE WiFi_OTA_Manager SHALL 不创建后台任务、不分配动态堆内存、不初始化 WiFi 射频硬件
2. WHEN WiFi OTA 模式退出后设备重启, THE Button_Detector SHALL 在启动完成后恢复所有按键功能，包括短按/长按检测、5 秒三键清除 WiFi 凭据手势、3 秒左右键进入配对模式手势，且 BLE GATT 通讯和 USB HID 键盘输出恢复正常数据传输
3. THE WiFi_OTA_Manager SHALL 与现有的串口/BLE OTA 功能（ota_manager 模块）互斥运行，同一时间仅一种 OTA 方式可激活
4. IF WiFi OTA 模式激活时收到串口/BLE OTA 的 ota_begin 请求, THEN THE ota_manager SHALL 拒绝该请求并返回错误响应指示 OTA 已被 WiFi OTA 占用
5. WHEN WiFi OTA 模式激活, THE WiFi_OTA_Manager SHALL 停止 BLE GATT 数据发送与接收、停止 USB HID 键盘报告输出、停止按键轮询（pollButtons），并保持串口 CDC 通道仅用于 WiFi OTA 自身的状态日志输出
6. WHEN WiFi OTA 模式激活, THE WiFi_OTA_Manager SHALL 在圆形屏和矩形屏上显示 OTA 进度界面，与现有串口 OTA 激活时的 UI 行为一致（复用 drawOtaRound/drawOtaRect 渲染逻辑）
