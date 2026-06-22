# 任务列表

## 任务 1：创建 WiFi OTA 核心模块骨架

- [ ] 1.1 创建 `src/wifi_ota.h`，定义 `WifiOtaState`、`WifiOtaError` 枚举和所有公共函数声明
- [ ] 1.2 创建 `src/wifi_ota.cpp`，实现 `WifiOtaContext` 静态结构体和基础状态查询函数（`wifiOtaIsActive()`、`wifiOtaGetState()`、`wifiOtaProgress()`、`wifiOtaGetError()`）
- [ ] 1.3 实现 `wifiOtaApSsid()` 和 `wifiOtaApPassword()`，复用 `wifi_config.h` 中的 `wifiDeviceCode()` 生成 "KiroKB-XXXXXX" 和 "kiro-XXXXXX" 格式凭据
- [ ] 1.4 在 `platformio.ini` 的 `build_src_filter` 中添加 `+<wifi_ota.cpp>`

## 任务 2：实现 OTA 状态机与生命周期管理

- [ ] 2.1 实现 `wifiOtaBegin()` 函数：检查互斥（`otaIsActive()`）→ 设置状态为 AP_STARTING → 停止 webconfig 服务 → 调用 `WiFi.mode(WIFI_AP)` + `WiFi.softAP()` → 成功则转为 AP_READY 状态
- [ ] 2.2 实现 `wifiOtaLoop()` 超时检测逻辑：AP_READY 阶段 300s 无活动 → TIMEOUT；UPLOADING 阶段块间 15s 或总计 180s → TIMEOUT
- [ ] 2.3 实现超时/错误后的重启逻辑：TIMEOUT 状态 5s 后调用 `ESP.restart()`；SUCCESS 状态 900ms 后调用 `ESP.restart()`
- [ ] 2.4 实现退出清理：释放 SHA-256 上下文、调用 `Update.abort()`（如果写入中）、停止 Web 服务器、断开 WiFi AP

## 任务 3：实现 OTA Web 服务器端点

- [ ] 3.1 创建 `src/wifi_ota_page.h`，编写 OTA 上传 HTML 页面（PROGMEM），包含文件选择（accept=".bin"）、上传按钮、进度条、版本号显示、响应式布局
- [ ] 3.2 实现 GET `/` 端点：返回 OTA HTML 页面
- [ ] 3.3 实现 GET `/ota/status` 端点：返回 JSON 格式的当前状态、进度、错误信息、固件版本
- [ ] 3.4 实现 POST `/ota/upload` 端点：使用 AsyncWebServer 的 multipart upload handler 接收文件流
- [ ] 3.5 在 upload handler 中实现大小验证：首块到达时检查 Content-Length，>7MB 或 ==0 时拒绝
- [ ] 3.6 在 upload handler 中实现流式写入：每块调用 `Update.write()` + `mbedtls_sha256_update()`
- [ ] 3.7 在 upload handler 的最终回调中实现 SHA-256 校验和 `Update.end(true)` 调用
- [ ] 3.8 实现上传互斥：`uploadInProgress` 标志阻止并发上传请求


## 任务 4：集成三键长按检测

- [ ] 4.1 在 `main.cpp` 的 `pollButtons()` 中新增三键 10s 长按检测逻辑：当三键全部按下且持续 ≥10000ms 时调用 `wifiOtaBegin()`
- [ ] 4.2 确保 10s OTA 触发与现有 5s WiFi 清除手势兼容：5s 时触发清除，继续按住到 10s 时触发 OTA
- [ ] 4.3 在 `pollButtons()` 开头添加 `wifiOtaIsActive()` 检查：OTA 激活时跳过所有按键处理（包括 HID、手势、配对）
- [ ] 4.4 添加 `PAIRING_PAIRING` 阶段的守卫条件：配对中时不响应三键长按 OTA 触发
- [ ] 4.5 在主循环 `loop()` 中添加 `wifiOtaLoop()` 调用和 OTA UI 刷新逻辑

## 任务 5：实现显示集成

- [ ] 5.1 在 `wifiOtaBegin()` 成功时调用 `drawOtaRect()` 显示 "Entering OTA..." 状态
- [ ] 5.2 AP 就绪后在矩形屏上显示 SSID、密码、IP 三项信息（自定义布局函数或复用 drawOtaRect 扩展）
- [ ] 5.3 复用 `drawOtaRound()` 和 `drawOtaRect()` 显示上传进度（0-100%），每收到一块数据后刷新
- [ ] 5.4 实现超时倒计时显示：每秒刷新一次剩余等待秒数（`lastDisplayMs` 控制刷新频率 ≥1000ms）
- [ ] 5.5 实现错误显示：根据 `WifiOtaError` 枚举值映射到对应的中文/英文错误提示字符串
- [ ] 5.6 成功时显示 "Update OK, rebooting" 并保持至少 800ms

## 任务 6：实现与现有模块的互斥协调

- [ ] 6.1 在 `ota_manager.cpp` 的 `handleBegin()` 中添加 `wifiOtaIsActive()` 检查：WiFi OTA 激活时拒绝串口/BLE OTA 请求
- [ ] 6.2 在 `wifiOtaBegin()` 中停止 BLE GATT 数据收发（调用相关停止函数或设置标志）
- [ ] 6.3 在 `wifiOtaBegin()` 中停止 USB HID 报告输出
- [ ] 6.4 在 `wifiOtaBegin()` 中停止现有 webconfig 服务（调用 server.end() 或重置服务器）
- [ ] 6.5 确保 WiFi OTA 模式下串口 CDC 通道保持工作（仅用于日志输出）

## 任务 7：编写属性测试

- [ ] 7.1 编写属性测试：三键触发充要条件 — 生成随机按键时序验证触发逻辑正确性
- [ ] 7.2 编写属性测试：AP 凭据格式 — 生成随机 MAC 地址验证 SSID/密码格式和一致性
- [ ] 7.3 编写属性测试：固件大小验证 — 生成随机 size 验证接受/拒绝边界
- [ ] 7.4 编写属性测试：SHA-256 分块一致性 — 生成随机数据验证分块计算等于整体计算
- [ ] 7.5 编写属性测试：进度百分比计算 — 生成随机 written/total 验证百分比正确性和范围
- [ ] 7.6 编写属性测试：超时判定 — 生成随机时间戳验证超时阈值判定正确性
- [ ] 7.7 编写属性测试：OTA 互斥 — 验证两种 OTA 方式不能同时激活
- [ ] 7.8 编写属性测试：错误信息映射 — 验证每种错误枚举对应非空唯一提示文本

## 任务 8：编写单元测试与集成验证

- [ ] 8.1 编写单元测试：BLE 配对阶段不触发 OTA
- [ ] 8.2 编写单元测试：AP 启动失败时状态转为 ERROR 并在 5s 后重启
- [ ] 8.3 编写单元测试：校验失败后 Update.end(true) 未被调用
- [ ] 8.4 编写单元测试：成功路径下 900ms 后 ESP.restart() 被调用
- [ ] 8.5 编写集成测试：完整 OTA 流程（AP 启动 → 文件上传 → 校验 → 重启）在 native 平台模拟验证
