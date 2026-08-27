# mdc_lite — 极简调用库（MicroPython 实现：RP2040）

> **独立实现**（自带 CRC8/组帧/解析，不依赖 `mdc_lib`），只服务于一个核心场景：
> **上位机调参、下位机执行** —— 你只需告诉下位机要发什么控制量，并从下位机拿回实时转速。
> 除此之外的文本指令、config_t 全字段读写、SBUS/检测/波特率识别等**一律不管**。
> **完全独立实现（零依赖）**：mdc_lite 自带 CRC8、组帧与 `0xF0` 流式解析，不 import `mdc_lib` / `struct` / `machine` / `math`，串口收发由你实现。
> 规范依据：[`../../LITE.md`](../../LITE.md)；底层协议：[`../../协议规范.md`](../../协议规范.md)。

---

## 一、功能 / 范围

只涉及这 **4 条二进制命令**：

| CMD | 名称 | 用途 |
|:---:|------|------|
| `0x31` | MOTOR_CTRL | 发送四通道控制目标（下位机执行） |
| `0x40` | SUBSCRIBE | 开启状态周期上报（收速度的前提） |
| `0x41` | UNSUBSCRIBE | 关闭状态上报（善后） |
| `0xF0` | STATUS_REPORT | 下位机主动上报；从中取出 `rpm[4]` 作为速度回调 |

**绝不涉及**：文本指令、`config_t`/SBUS/检测/波特率识别。**不碰串口**（返回要发送的字节或解析喂入的字节，收发由用户实现）。

## 二、文件清单

| 文件 | 说明 |
|------|------|
| `mdc_lite.py` | **只管调用（send-only）**：`ctrl / stop / subscribe / unsubscribe` 返回要发送的整帧 `bytes` |
| `mdc_lite_ctrl.py` | **调用+回调接收**：`MDLite(on_speed)` 在 send-only 基础上流式接收 `0xF0` 并回调 `on_speed(rpm)` |
| `mdc_lib.py` | 完整版底层库（API.md）。**mdc_lite 已是独立实现、不再依赖它**；仅需完整能力（文本指令 / config 读写等）时才用 |

> **三平台共用同一份 mdc_lite 代码**：`esp32/micropython`、`rp2040/micropython`、`esp8266/micropython` 下的 `mdc_lite.py` / `mdc_lite_ctrl.py` / `test_mdc_lite.py` 内容完全一致（哈希一致），仅本 README 的接线/引脚不同。

## 三、API 速览

**① mdc_lite（send-only，只管发送）** —— 每个函数返回 `bytes`，直接 `uart.write`：

```python
import mdc_lite
uart.write(mdc_lite.ctrl(100, -200, 0, 300))   # 0x31 四通道控制帧
uart.write(mdc_lite.stop())                    # 0x31 全零（急停）
uart.write(mdc_lite.subscribe(50))             # 0x40 开启 50ms 状态上报
uart.write(mdc_lite.unsubscribe())             # 0x41 关闭上报
```

**② mdc_lite_ctrl（调用+回调接收）** —— 发送侧同上，另加速度回调：

```python
from mdc_lite_ctrl import MDLite
mdc = MDLite(on_speed)            # on_speed(rpm) -> 收到 0xF0 时调用，rpm 为 4 元转速
uart.write(mdc.subscribe(50))     # 收速度的前提
uart.write(mdc.ctrl(100, 0, 0, 0))
for b in uart.read(n):
    mdc.feed(b)                   # 0xF0 完整帧到达时自动回调 on_speed
```

## 四、串口接入示例（UART0，TX=GPIO0 / RX=GPIO1）

### 4.1 接线（RP2040 UART0，3.3V 逻辑电平，与控制板**共地**）

| RP2040 | 控制板 |
|--------|--------|
| GPIO0（UART0 TX） | RC 口信号（USART2 RX） |
| GPIO1（UART0 RX） | RC 口信号（USART2 TX） |
| GND | GND（必须共地） |

> RP2040 的 UART0 默认即 GPIO0/GPIO1，也可改用 UART1（GPIO4/GPIO5），只需改 `UART(1, ..., tx=Pin(4), rx=Pin(5))`。控制板 **USB 口固定 2000000-8N1**（CH340N），波特率不可改；**RC 口（USART2）** 波特率由 `/uart2` 决定，本示例走 RC 口、波特率 115200。

### 4.2 send-only 示例（只发控制帧）

```python
from machine import Pin, UART
import mdc_lite          # send-only：只管打包要发送的帧

uart = UART(0, baudrate=115200, tx=Pin(0), rx=Pin(1), timeout=50)

uart.write(mdc_lite.ctrl(100, -200, 0, 300))   # 0x31 四通道目标值
uart.write(mdc_lite.ctrl(0, 0, 0, 0))          # 归零
uart.write(mdc_lite.stop())                    # 急停（0x31 DATA 16B 全零）
# 需回读转速时先订阅：
uart.write(mdc_lite.subscribe(50))             # 0x40，固件钳位 ≥20ms
# ... 接收由 mdc_lite_ctrl 处理（见下节）...
uart.write(mdc_lite.unsubscribe())             # 0x41，善后
```

### 4.3 调用+回调示例（发控制帧 + 收速度回调）

```python
from machine import Pin, UART
from mdc_lite_ctrl import MDLite

def on_speed(rpm):
    print("实时转速 rpm:", rpm)     # rpm = [m0, m1, m2, m3]

uart = UART(0, baudrate=115200, tx=Pin(0), rx=Pin(1), timeout=50)
mdc = MDLite(on_speed)              # 注册速度回调
uart.write(mdc.subscribe(50))       # 先订阅（收速度的前提）

while True:
    uart.write(mdc.ctrl(100, -200, 0, 300))   # 发送控制帧
    n = uart.any()
    if n:
        for b in uart.read(n):                # 把收到的字节喂给接收器
            mdc.feed(b)                       # 0xF0 到达时自动回调 on_speed
```

## 五、集成步骤

1. 把 `mdc_lite.py`、`mdc_lite_ctrl.py`（按需）复制到 RP2040 文件系统（Thonny / ampy / WebREPL 均可），与你的 `main.py` **同目录**。`mdc_lite` 是独立实现，无需 `mdc_lib.py`；仅当需要完整能力（文本指令 / config 读写等）时才一并复制 `mdc_lib.py`。
2. **接线**（见 §4.1），与控制板**共地**。
3. 控制板 **USART2（RC 口）** 需先配置为 UART 模式（用 USB 口或 RC 口发一次）：
   ```
   /uart2 115200 0 uart
   ```
   之后 RC 口即可收发，波特率与本示例 115200 保持一致。
4. 运行 `main.py`：发送用 `mdc_lite.ctrl(...)` / `stop()` / `subscribe()` / `unsubscribe()` 返回的 `bytes`，接收喂给 `MDLite.feed(b)`（回调在 `on_speed` 打印）。
5. 实时控制注意：控制板 **RC 口默认优先**（`/priority 0`），控制帧天然生效；外部上位机经 USB 实时控制需先 `/priority 1`；确认 `/timeout` 未超时归零。

## 六、与 mdc_lib 的关系

- mdc_lite **独立实现协议**：自带 CRC8、组帧与 `0xF0` 流式解析，不 import `mdc_lib`。字节布局（LE）、CRC8（0x07，初值 0）、帧格式 `[AA][CMD][LEN][DATA][CRC8]` 与 `mdc_lib` **完全一致**；`mdc_lite.ctrl(...)` 等价于 `mdc_lib.md_bin_motor_ctrl(...)`。
- 一个工程只需发送 → 引 `mdc_lite`；需回读转速 → 引 `mdc_lite_ctrl`（同时获得发送与回调）。二者均**不依赖 `mdc_lib`**。
- 若想用完整能力（文本指令 / config 读写 / SBUS / 检测），直接改用本目录 `mdc_lib.py` 即可，与本文件的 mdc_lite 兼容共存。

## 七、一致性验证

- 已通过 `python -m py_compile`（三平台）对 `mdc_lite.py` / `mdc_lite_ctrl.py` 语法检查。
- `test_mdc_lite.py`（零依赖自检，在库同目录运行 `python test_mdc_lite.py`，打印 `ALL OK`）覆盖：
  - CRC 向量：`crc8([0x01,0x00])=0x15`、`crc8(b"123456789")=0xF4`
  - `ctrl(100,-200,0,300)` DATA 段 `64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00`
  - `subscribe(50)` 帧 `AA 40 02 32 00 <crc>`；`unsubscribe` CMD=0x41；`stop` 全零帧
  - mdc_lite_ctrl 流式解析：噪声/0x31/坏 CRC 帧**不**触发回调；56B 与 72B `0xF0` 帧触发一次回调且 `rpm` 四值正确
