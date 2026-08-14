# 03 实时电机控制（ESP32 + MicroPython）

## 功能
- 通过二进制控制帧 **0x31 MOTOR_CTRL**（int32 LE ×4）实时控制电机
- 演示序列：开环**正转 → 反转 → 停止**（每档 2 秒，斜坡平滑过渡，只动通道 1）
- 之后进入**交互模式**：REPL 输入目标值（如 `400` / `-200` / `0`），`q` 退出
- 斜坡实现：每个 50ms 发送周期向目标值靠近固定步长，到达后保持发送（防止 `/timeout` 超时归零）

## 硬件接线（ESP32 UART2 ↔ 控制板 RC 口）

| ESP32 引脚 | 方向 | 控制板 RC 口 |
|-----------|:---:|------------|
| GPIO17（UART2 TX） | → | RC 信号（USART2 RX） |
| GPIO16（UART2 RX） | ← | RC 信号（USART2 TX） |
| GND | — | GND（**必须共地**） |

文字接线图：

```
控制板 RC 口 (USART2)            ESP32 (MicroPython)
┌───────────────────┐          ┌───────────────────┐
│  RC 信号 (RX)     │◄─────────│  GPIO17 (UART2 TX) │
│  RC 信号 (TX)     │─────────►│  GPIO16 (UART2 RX) │
│  GND              │──────────│  GND               │
└───────────────────┘          └───────────────────┘
```

电机接在控制板 **Motor A**（通道 1）输出端；如需其他通道，改 `main.py` 顶部的 `CH`（0~3）。

## 控制板预配置（必须）
先用 USB 线连接控制板（串口参数 **2000000-8N1**），发送：

```
/uart2 115200 0 uart
```

将 USART2 配置为 **UART 模式**（115200、极性正常、模式 uart），立即生效。

### ⚠️ 目标值的含义随控制模式变化（关键！）
| 控制模式 | 目标值含义 | 示例 |
|---------|-----------|------|
| 开环（默认） | PWM，±1000 | `300` = 30% 正向占空比 |
| 速度闭环 | RPM | `500` = 500 RPM |
| 位置闭环 | 0.1° | `3600` = 360.0° |

- 本例程默认按**开环 PWM** 演示；如要跑速度/位置闭环，需先用文本指令配置：
  ```
  /cpr 1 400          # 通道 1 编码器线数（0 = 强制开环）
  /speedctrl 1 0.3 0.05 0.01   # 速度环 PID（可选更多参数）
  /posctrl 1 5 0 0.1            # 位置环 PID
  /mode 1 speed                 # 或 /mode 1 pos
  ```
  PID/CPR 参数需按实际电机标定，配置后 `main.py` 中的 `TARGET_*` 就变成 RPM / 0.1° 单位。
- 控制优先级：默认 `/priority 0` = **USART2 优先**，本板（USART2 侧）发控制帧**天然生效**；若 USB 上位机也在同时发控制帧，需注意仲裁（非优先端口要等优先端口失联超过 `/timeout` 窗口才能接管，规范 §3.4）。

## 文件上传与运行（三选一）

### 方式一：Thonny（推荐）
1. 打开 Thonny → 选择解释器 MicroPython (ESP32) 及对应串口
2. 把 `motor_driver.py`、`main.py` 保存到设备（文件名不变）
3. 打开 `main.py` 点击运行 ▶

### 方式二：ampy（命令行）
```
ampy --port COM5 put motor_driver.py
ampy --port COM5 put main.py
ampy --port COM5 reset
```

### 方式三：mpremote（命令行）
```
mpremote connect COM5 cp motor_driver.py :
mpremote connect COM5 cp main.py :
mpremote connect COM5 reset
```

> 交互模式需要 REPL 输入：Thonny 运行即可在下方 Shell 输入；ampy 方式下 `input()` 会因无 REPL 输入而直接退出（只跑演示序列），属预期行为。

## 代码结构
- `motor_driver.py`：二进制协议封装库（与 02 例程同源，自包含，直接复制）
- `main.py`：
  - 顶部可调参数：`CH` / `STEP` / `PERIOD_MS` / `TARGET_FWD` / `TARGET_REV` / `TARGET_STOP` / `PHASE_MS` / `HOLD_MS`
  - `ramp_to()`：斜坡逼近目标 + 周期发送 0x31
  - `demo_sequence()`：正转 → 反转 → 停止
  - `interactive()`：REPL 输入目标值循环
  - `finally` 安全收尾：退出时发送全零停止帧

## 用到的协议命令
| CMD | 名称 | 说明 |
|:---:|------|------|
| 0x31 | MOTOR_CTRL | 四通道批量控制（int32 LE ×4），本例只动通道 1 |
| （文本）`/uart2 115200 0 uart` | USART2 配置 | 前置必做 |
| （文本）`/cpr` `/speedctrl` `/posctrl` `/mode` | 闭环配置 | 速度/位置闭环前必做 |

## 常见问题
| 现象 | 处理 |
|------|------|
| 电机不动 | 确认已 `/uart2 115200 0 uart`；确认目标值符号/大小合理（开环 ±1000）；确认电机接在 Motor A |
| 控制帧无效 | 默认 USART2 优先，本板天然生效；若 USB 上位机同时在发控制帧，注意仲裁；协议识别（/detect）期间控制帧被拒绝 |
| 电机转一下就停 | `/timeout` 超时保护触发：本例程每 50ms 持续发送，检查发送周期是否被阻塞；或把 `/timeout` 调大/置 0 |
| 转速/位置不对 | 目标值单位随控制模式变化：速度=RPM、位置=0.1°；确认已配 CPR + PID 并 `/mode` 切换 |
| 方向反了 | `/inv`（引脚反转）或 `/einv`（编码器极性）调整 |
| PING 超时/无响应 | 检查接线、共地、波特率 |
