# 控制+回调 — 发送控制帧 + 收速度回调（ESP32 + MicroPython，基于 mdc_lite_ctrl）

## 功能
- 演示「极简调用库」的 **调用+回调接收** 形态：既发送控制帧，又在收到 `0xF0 STATUS_REPORT` 时把四通道转速回调给用户
- 用 `MDLite(on_speed)` 注册速度回调；`mdc.subscribe(interval_ms)` 先开启 **0x40** 状态上报
- 主循环：`mdc.ctrl(...)` 发送 **0x31** 控制帧；UART 收到的字节逐字节 `mdc.feed(b)`，`0xF0` 完整帧到达时自动回调 `on_speed(rpm)`
- 退出时自动 `mdc.stop()` 急停 + `mdc.unsubscribe()` 关闭上报（0x41 善后）
- 用途：看懂"发控制 + 收转速"的最简闭环；只发不收请看同级的 **极简控制** 例程

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
- 本例程只依赖 MicroPython 标准库（`machine` / `utime`）与 **mdc_lite_ctrl**（及底层 **mdc_lite** / **mdc_lib**）。
- `mdc_lib.py`、`mdc_lite.py`、`mdc_lite_ctrl.py` 已随例程内置（本目录），上传整个例程文件夹到设备即可运行（`main.py` 直接 `from mdc_lite_ctrl import MDLite`）；如需更新库版本，用 `../../../mdc_lib/esp32/micropython/mdc_lite_ctrl.py`（连同 `mdc_lite.py`、`mdc_lib.py`）覆盖。
- mdc_lite 是纯计算库（零依赖），只负责**打包帧**与**解析回包**，不碰串口；串口收发在例程里自己实现。

## mdc_lite 调用指南
本例程用到的 mdc_lite API（`MDLite` 类，发送侧复用 `mdc_lite`，接收侧新增接口）：

| API | 返回/行为 | 说明 |
|-----|------|------|
| `MDLite(on_speed)` | 实例 | 注册速度回调 `on_speed(rpm)`；`rpm` 为 4 元 int32 转速 |
| `mdc.ctrl(m0, m1, m2, m3)` | `bytes` | 打包 0x31 MOTOR_CTRL 帧（同 `mdc_lite.ctrl`） |
| `mdc.stop()` | `bytes` | 四通道全零控制帧（急停，同 `mdc_lite.stop`） |
| `mdc.subscribe(interval_ms)` | `bytes` | 打包 0x40 SUBSCRIBE 帧（`[interval_ms:2B LE]`，固件钳位 ≥20ms） |
| `mdc.unsubscribe()` | `bytes` | 打包 0x41 UNSUBSCRIBE 帧（退出时发送） |
| `mdc.feed(byte)` | `None` | 流式喂入一个字节；收到完整且 CRC 通过、CMD=0xF0 的帧时解析 `rpm[4]` 并回调 `on_speed(rpm)`；其他帧/噪声忽略 |
| `mdc.reset()` | `None` | 清空流式解析器缓冲（切换连接/重新同步时调用） |

实际调用示例（节选自 `main.py`）：

```python
from mdc_lite_ctrl import MDLite

def on_speed(rpm):
    print("实时转速 rpm:", rpm)      # rpm = [m0, m1, m2, m3]

mdc = MDLite(on_speed)               # 注册速度回调
uart.write(mdc.subscribe(50))        # 先订阅（收速度的前提）

while True:
    uart.write(mdc.ctrl(100, -200, 0, 300))   # 发送控制帧
    n = uart.any()
    if n:
        for b in uart.read(n):
            mdc.feed(b)                      # 0xF0 到达时自动回调 on_speed
```

## 文件上传与运行（三选一）

### 方式一：Thonny（推荐）
1. 打开 Thonny → 选择解释器 MicroPython (ESP32) 及对应串口
2. 依次把 `mdc_lib.py`、`mdc_lite.py`、`mdc_lite_ctrl.py`、`main.py` 保存到设备（**同目录**）
3. 打开 `main.py` 点击运行 ▶；Ctrl+C 退出（退出时自动急停 + 关闭上报）

### 方式二：ampy（命令行）
```
ampy --port COM5 put mdc_lib.py
ampy --port COM5 put mdc_lite.py
ampy --port COM5 put mdc_lite_ctrl.py
ampy --port COM5 put main.py
ampy --port COM5 reset
```

### 方式三：mpremote（命令行）
```
mpremote connect COM5 cp mdc_lib.py :
mpremote connect COM5 cp mdc_lite.py :
mpremote connect COM5 cp mdc_lite_ctrl.py :
mpremote connect COM5 cp main.py :
mpremote connect COM5 reset
```

> `COM5` 换成 ESP32 在电脑上实际出现的串口号。

## 代码结构
- `main.py`：
  - 顶部可调参数：`UART_ID` / `TX_PIN` / `RX_PIN` / `BAUD` / `SUBSCRIBE_MS` / `CTRL_PERIOD_MS` / `TARGET` / `CH`
  - `on_speed(rpm)`：速度回调，打印四通道转速
  - `main()`：`mdc.subscribe()` 开启上报 → 循环 `mdc.ctrl()` 发送控制帧 + 读 UART 字节喂 `mdc.feed()` → 回调打印；`finally` 里 `mdc.stop()` + `mdc.unsubscribe()` 收尾

## 常见问题
| 现象 | 处理 |
|------|------|
| 一直无转速回调 | 检查接线、共地；确认已 `/uart2 115200 0 uart`；确认 `subscribe()` 成功且 `SUBSCRIBE_MS` ≥ 20 |
| 电机不动 | RC 口默认优先（/priority 0），控制帧天然生效；确认 `/timeout` 未超时归零 |
| 回调偶尔漏 | 协议识别（`/detect` 或 FUN 键）期间控制帧被拒绝；确认另一侧没有占用 RC 口 |
| ImportError: no module named 'mdc_lite_ctrl' | `mdc_lite_ctrl.py` / `mdc_lite.py` / `mdc_lib.py` 未上传或不在 `main.py` 同目录 |
