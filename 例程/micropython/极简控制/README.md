# 极简控制 — 只管发送控制帧（ESP32 + MicroPython，基于 mdc_lite）

## 功能
- 演示「极简调用库」的 **send-only** 形态：只打包并发送要发的控制帧，**不解析任何回包**
- 用 `mdc_lite.ctrl(m0, m1, m2, m3)` 打包 **0x31 MOTOR_CTRL** 帧、`mdc_lite.stop()` 打包全零帧，经 UART 发送
- 演示序列：开环正转 → 反转 → 停止（每档 `PHASE_MS`，斜坡平滑过渡）
- 可选 `mdc_lite.subscribe(interval_ms)` / `mdc_lite.unsubscribe()`（0x40/0x41），因本例不读回包故仅作说明
- 用途：看懂"发什么"这一侧的最简代码；要收转速回调请看同级的 **控制+回调** 例程

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

> ⚠️ 接的是控制板 **RC 接口（USART2）**，不是 Type-C USB 口（USB 口波特率固定 2000000-8N1，只给电脑串口工具用）。

## 控制板预配置
先用 USB 线连接控制板到电脑（USB 虚拟串口参数固定 **2000000-8N1**），在串口调试工具中发送：

```
/uart2 115200 0 uart
```

将 USART2 配置为 **UART 模式**（波特率 115200、极性正常、模式 uart），立即生效，无需 `/save`。

## 依赖与 mdc_lite
- 本例程只依赖 MicroPython 标准库（`machine` / `utime`）与 **mdc_lite**（及底层 **mdc_lib**）。
- `mdc_lib.py`、`mdc_lite.py` 已随例程内置（本目录），上传整个例程文件夹到设备即可运行（`main.py` 直接 `import mdc_lite`）；如需更新库版本，用 `../../../mdc_lib/esp32/micropython/mdc_lite.py`（连同 `mdc_lib.py`）覆盖。
- mdc_lite 是纯计算库（零依赖），只负责**打包要发送的帧**，不碰串口；串口收发在例程里自己实现。

## mdc_lite 调用指南
本例程用到的 mdc_lite API：

| API | 返回 | 说明 |
|-----|------|------|
| `mdc_lite.ctrl(m0, m1, m2, m3)` | `bytes` | 打包 0x31 MOTOR_CTRL 帧（四通道 int32 LE）；目标值含义随通道模式：开环=PWM(±1000) / 速度=RPM / 位置=0.1°(±3600) |
| `mdc_lite.stop()` | `bytes` | 便捷：四通道全零控制帧（急停/退出前发送） |
| `mdc_lite.subscribe(interval_ms)` | `bytes` | 打包 0x40 SUBSCRIBE 帧（`[interval_ms:2B LE]`，固件钳位 ≥20ms） |
| `mdc_lite.unsubscribe()` | `bytes` | 打包 0x41 UNSUBSCRIBE 帧，关闭上报 |

实际调用示例（节选自 `main.py`）：

```python
import mdc_lite

uart.write(mdc_lite.ctrl(100, -200, 0, 300))   # 0x31 四通道控制帧
uart.write(mdc_lite.ctrl(0, 0, 0, 0))          # 归零
uart.write(mdc_lite.stop())                    # 急停（0x31 DATA 16B 全零）
# 需回读转速时先订阅（接收侧见"控制+回调"例程）：
uart.write(mdc_lite.subscribe(50))             # 0x40
uart.write(mdc_lite.unsubscribe())             # 0x41
```

## 文件上传与运行（三选一）

### 方式一：Thonny（推荐）
1. 打开 Thonny → 选择解释器 MicroPython (ESP32) 及对应串口
2. 依次把 `mdc_lib.py`、`mdc_lite.py`、`main.py` 保存到设备（**同目录**）
3. 打开 `main.py` 点击运行 ▶；Ctrl+C 退出（退出时自动 0x31 全零急停）

### 方式二：ampy（命令行）
```
ampy --port COM5 put mdc_lib.py
ampy --port COM5 put mdc_lite.py
ampy --port COM5 put main.py
ampy --port COM5 reset
```

### 方式三：mpremote（命令行）
```
mpremote connect COM5 cp mdc_lib.py :
mpremote connect COM5 cp mdc_lite.py :
mpremote connect COM5 cp main.py :
mpremote connect COM5 reset
```

> `COM5` 换成 ESP32 在电脑上实际出现的串口号。

## 代码结构
- `main.py`：
  - 顶部可调参数：`UART_ID` / `TX_PIN` / `RX_PIN` / `BAUD` / `CH` / `STEP` / `PERIOD_MS` / `TARGET_*` / `PHASE_MS`
  - `send_ctrl(current)`：`mdc_lite.ctrl()` 打包 0x31 帧并发送（只动 `CH` 通道，其余填 0）
  - `ramp_to(target, current)`：斜坡逼近目标并持续发送（防 `/timeout` 超时归零）
  - `main()`：演示序列「正转 → 反转 → 停止」；`finally` 里 `mdc_lite.stop()` 急停

## 常见问题
| 现象 | 处理 |
|------|------|
| 电机不动 | 检查接线、共地；确认已 `/uart2 115200 0 uart`；RC 口默认优先（/priority 0），控制帧天然生效 |
| 时灵时不灵 | 协议识别（`/detect` 或 FUN 键）期间控制帧被拒绝，等待识别完成 |
| 目标值过大 | 开环 ±1000、位置 ±3600，超出会被固件钳位；本例正转 300 / 反转 -300 |
| 一停就归零 | `/timeout` 超时归零属正常保护；持续发送 0x31 即保持（本例每 50ms 发送一次） |
| ImportError: no module named 'mdc_lite' | `mdc_lite.py` / `mdc_lib.py` 未上传或不在 `main.py` 同目录 |
