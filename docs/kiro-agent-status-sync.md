# Kiro Agent 状态同步方案

## 1. 目标

把 Kiro custom agent 的运行状态同步到键盘板子，让矩形 LCD 的四象限能显示当前活跃 agent 的状态和名字。

本方案基于 Kiro CLI 官方 hooks：

- `agentSpawn`
- `userPromptSubmit`
- `stop`
- `postToolUse`

Kiro hooks 会通过 STDIN 向 hook command 传入 JSON 事件。板子端接收简化后的 JSONL 状态消息。

## 2. 总体架构

```text
Ghostty split
  -> kiro-cli --agent <custom-agent-name> chat --trust-all-tools
  -> Kiro hook
  -> hook script
  -> USB Serial JSONL
  -> ESP32-S3
  -> Rect LCD 2x2 agent status
```

## 3. Custom Agent 约定

板子不再依赖 `KIRO_AGENT_SLOT`。每个 custom agent 在 `agentSpawn` 时向板子注册自己的名字和 session id。板子最多显示 4 个活跃 agent，空余区域显示 `EMPTY`。

第一版不强制固定 4 个预定义角色；custom agent 只要配置了 `name`，就可以被注册到板子上。当前试验配置提供 4 个示例名字：

| custom agent name | 说明 |
|-------------------|------|
| `planner` | 规划 / 拆解任务 |
| `coder` | 编码 / 改实现 |
| `reviewer` | 代码审查 / 风险检查 |
| `runner` | 执行 / 验证 / 烧录 |

每个 split 启动 Kiro CLI 时使用：

```bash
kiro-cli --agent planner chat --trust-all-tools
kiro-cli --agent coder chat --trust-all-tools
kiro-cli --agent reviewer chat --trust-all-tools
kiro-cli --agent runner chat --trust-all-tools
```

`session_id` 由 Kiro hook 事件提供，用于日志、排错和区分同名 agent 的不同会话。

### 3.1 注册规则

1. `agentSpawn` 到来时，板子把 `agent_name` 记入 registry。
2. 同名 `agent_name` 再次出现时，更新原有条目的 `session_id` 和状态。
3. `stop` 不释放条目，只把该条目标成 `idle`。
4. 如果已满 4 个活跃条目，而又来了一个新名字，则替换最久未更新的条目。

## 4. Hook 到状态的映射

| Kiro Hook | 触发含义 | 板子状态 |
|-----------|----------|----------|
| `agentSpawn` | agent session 初始化 / 激活 | 注册 / `idle` |
| `userPromptSubmit` | 用户提交 prompt | `running` |
| `stop` | assistant 本轮回复结束 | `idle` |
| `postToolUse` | 工具执行完成 | 工具失败时可选 `error` |

第一版只需要：

```text
agentSpawn       -> idle
userPromptSubmit -> running
stop             -> idle
```

`postToolUse -> error` 可以后续再做，因为不同工具失败不一定代表整个 agent 失败。

## 5. 板子串口协议

Hook script 向板子发送 JSON Lines，每行一个事件。

### 5.1 Agent 状态事件

```json
{"type":"agent_state","agent_name":"planner","state":"running","session_id":"abc123"}
```

字段：

| 字段 | 类型 | 说明 |
|------|------|------|
| `type` | string | 固定为 `agent_state` |
| `agent_name` | string | custom agent 名字，对应矩形屏区域；板子用它做 registry key |
| `state` | string | `idle` / `running` / `error` |
| `session_id` | string | Kiro session id，用于调试，可选 |

### 5.2 示例

```json
{"type":"agent_state","agent_name":"planner","state":"idle","session_id":"s1"}
{"type":"agent_state","agent_name":"planner","state":"running","session_id":"s1"}
{"type":"agent_state","agent_name":"planner","state":"idle","session_id":"s1"}
```

## 6. Hook Script 设计

脚本实现放在 [scripts/kiro_board_hook.py](/Users/qiaoshi/Developer/vibe-coding-keyboard/scripts/kiro_board_hook.py)。

它做三件事：

1. 从 STDIN 读取 Kiro hook JSON。
2. 从命令行参数读取 `--agent-name`。
3. 把状态事件写成 JSONL，自动发现并写入板子的 USB Serial。

命令示例（零配置，自动发现串口）：

```bash
python3 scripts/kiro_board_hook.py --agent-name planner
```

脚本会通过 USB 自动发现机制找到 ki-board 设备并写入。如果没有找到板子，则回退到 stdout 模式，便于在终端调试 hook 输入。

如需手动指定串口（优先级高于自动发现）：

```bash
python3 scripts/kiro_board_hook.py --agent-name planner --serial-port /dev/cu.usbmodem14C19F35A9082
```

## 7. Agent 配置示例

Kiro custom agent configuration 中 hooks 需要调用同一个 script，并把 agent 名字硬编码进去。每个 custom agent 的配置文件里都可以复用同一个 hook 结构，只改 `--agent-name`。

```json
{
  "hooks": {
    "agentSpawn": [
      {
        "command": "python3 /path/to/ki-board/scripts/kiro_board_hook.py --agent-name planner"
      }
    ],
    "userPromptSubmit": [
      {
        "command": "python3 /path/to/ki-board/scripts/kiro_board_hook.py --agent-name planner"
      }
    ],
    "stop": [
      {
        "command": "python3 /path/to/ki-board/scripts/kiro_board_hook.py --agent-name planner"
      }
    ],
    "postToolUse": [
      {
        "command": "python3 /path/to/ki-board/scripts/kiro_board_hook.py --agent-name planner"
      }
    ]
  }
}
```

无需手动指定 `--serial-port`，脚本会自动发现 ki-board 设备。如果你想把启动和 hook 调用拆开，推荐先用脚本启动每个 split，再复用同一个 hook script。

启动脚本见 [scripts/kiro_agent_start.sh](/Users/qiaoshi/Developer/vibe-coding-keyboard/scripts/kiro_agent_start.sh)。

一个最小的启动方式是：

```bash
./scripts/kiro_agent_start.sh planner chat --trust-all-tools
./scripts/kiro_agent_start.sh coder chat --trust-all-tools
./scripts/kiro_agent_start.sh reviewer chat --trust-all-tools
./scripts/kiro_agent_start.sh runner chat --trust-all-tools
```

如果你希望把 hook 配置单独保存成样例，可以直接参考 [docs/kiro-cli-hooks.example.json](/Users/qiaoshi/Developer/vibe-coding-keyboard/docs/kiro-cli-hooks.example.json)。

本地试验用的 custom agent 配置样例放在 [.kiro/agents/](/Users/qiaoshi/Developer/vibe-coding-keyboard/.kiro/agents)。

### 7.1 Ghostty 启动约定

每个 split 启动 Kiro CLI 时，直接选择对应 custom agent：

```bash
kiro-cli --agent planner chat --trust-all-tools
kiro-cli --agent coder chat --trust-all-tools
kiro-cli --agent reviewer chat --trust-all-tools
kiro-cli --agent runner chat --trust-all-tools
```

建议把这四条命令封成 shell alias 或启动脚本，避免每次手敲。

### 7.2 Hook 到板子的连接方式

第一版推荐零配置用法，脚本会自动发现 ki-board 设备：

```bash
python3 /path/to/ki-board/scripts/kiro_board_hook.py --agent-name planner
```

如果你先想看 hook 事件内容，不连板子也可以设置环境变量 `KIRO_BOARD_PORT=stdout` 或拔掉板子让自动发现失败，脚本将回退到 stdout 打印。

如需手动指定串口（例如有多块板子时）：

```bash
python3 /path/to/ki-board/scripts/kiro_board_hook.py --agent-name planner --serial-port /dev/cu.usbmodem14C19F35A9082
```

## 8. 串口连接策略

### 8.1 第一版：Hook 直接写串口

优点：

1. 实现简单。
2. 不需要常驻 companion app。
3. 便于调试。

风险：

1. 多个 hook 同时写串口可能冲突。
2. 每次打开串口可能导致板子复位或短暂不可用。

### 8.2 如果第一版不稳定：增加轻量 bridge

如果直接写串口不稳定，再增加一个常驻 bridge：

```text
hook script -> append /tmp/kiro-board-events.jsonl
serial bridge -> 持续打开串口 -> 转发 JSONL 到板子
```

bridge 只负责传输，不负责推断状态。

### 8.3 自动发现串口

从 v2 开始，hook 脚本支持 USB 自动发现，无需手动指定 `--serial-port`。

#### 工作原理

脚本使用 `serial.tools.list_ports` 枚举系统上的 USB 串口设备，通过以下条件匹配 ki-board：

| 匹配条件 | 值 | 说明 |
|----------|------|------|
| USB VID | `0x303A` | Espressif 官方 Vendor ID |
| USB PID | `0x1001` | ESP32-S3 原生 USB CDC 默认 Product ID |
| Product 字符串 | `ki-board` | 固件通过 `USB_PRODUCT` 编译标志设置 |

固件端在 `platformio.ini` 中配置了 `-DUSB_PRODUCT="\"ki-board\""` 和 `-DUSB_MANUFACTURER="\"Kiro\"`，使得板子在 USB 枚举时广播可识别的产品名称。

#### 优先级链

端口解析遵循以下优先级（从高到低）：

1. `--serial-port` 命令行参数 - 明确指定，最高优先
2. `KIRO_BOARD_PORT` 环境变量 - 适合固定开发环境
3. USB 自动发现 - 零配置，适合大多数场景
4. stdout 回退 - 没有找到板子时打印到终端，便于调试

#### 验证板子是否可被发现

连接板子后，运行以下命令查看 USB 串口设备列表：

```bash
python3 -m serial.tools.list_ports -v
```

输出中应该能看到包含 `VID:PID=303A:1001` 和 `ki-board` 的设备条目。如果看到该条目，说明自动发现可以正常工作。

#### 换电脑 / 换 USB 口的场景

自动发现的核心优势是不依赖固定的设备路径。无论 `/dev/cu.usbmodemXXX` 的编号如何变化，只要板子通过 USB 连接且固件正确烧录，脚本都能通过 VID/PID + product 字符串匹配到正确的 CDC 串口。

## 9. 板子端处理

板子收到 JSONL 后：

1. 解析 `type == "agent_state"`。
2. 读取 `agent_name`，找到同名条目；如果没有同名条目，则注册到空闲槽位，满了就替换最久未更新条目。
3. 映射 `state`：
   - `idle` -> `AGENT_IDLE`
   - `running` -> `AGENT_RUNNING`
   - `error` -> `AGENT_ERROR`
4. 刷新矩形屏四象限。
5. 如果更新的是当前选中 Agent，也刷新圆形屏表情。

## 10. 本地乐观更新与 hook 校准

板子仍然保留本地乐观更新：

1. 用户按 key_middle 发送输入后，板子立即把当前 Agent 设为 `running`。
2. Kiro 的 `userPromptSubmit` hook 后续也会发送 `running`，用于校准。
3. Kiro 的 `stop` hook 发送 `idle`，让板子知道 agent 本轮完成，但不删除 registry 条目。

这样用户操作后 UI 立即响应，同时最终状态由 Kiro hook 校准。
