# 02 二进制协议封装（ESP32 + MicroPython）

## 功能
- `motor_driver.py`：**二进制帧协议封装库** —— CRC8 / 组帧 / ACK 校验 / 滑动窗口收帧
- `main.py`：Demo —— PING 连通性测试 + READ_PARAM 读取 config_t（231B）并打印
- 库已封装常用命令：PING / READ_PARAM / SAVE_EEPROM / MOTOR_CTRL / SUBSCRIBE / UNSUBSCRIBE / DEBUG_SPEED

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

> ⚠️ 接的是控制板 **RC 接口（USART2）**，不是 Type-C USB 口（USB 口波特率固定 2000000-8N1）。

## 控制板预配置（必须）
先用 USB 线连接控制板（串口参数 **2000000-8N1**），发送：

```
/uart2 115200 0 uart
```

将 USART2 配置为 **UART 模式**（波特率 115200、极性正常、模式 uart），立即生效，无需 `/save`。

## 文件上传与运行（三选一）

### 方式一：Thonny（推荐）
1. 打开 Thonny → 选择解释器 MicroPython (ESP32) 及对应串口
2. 依次把 `motor_driver.py`、`main.py` 保存到设备（文件名不变）
3. 打开 `main.py` 点击运行 ▶（或将 ESP32 复位自动运行）

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

> `COM5` 换成 ESP32 实际串口号。`main.py` 会 `from motor_driver import MotorDriver`，**两个文件都要上传**。

## 代码结构
- `motor_driver.py`：
  - 顶部常量：`UART_ID` / `TX_PIN` / `RX_PIN` / `BAUD` / `TIMEOUT_MS` / `RXBUF` + 18 条命令字
  - `crc8(data)`：多项式 0x07、初值 0、按位计算（与规范 C 参考实现等价）
  - `build_frame(cmd, data)`：组帧 `[0xAA][CMD][LEN][DATA][CRC8]`
  - `class MotorDriver`：
    - `send_frame(cmd, data)` / `read_frame(timeout_ms)`（滑动窗口找 0xAA → 按 LEN 收满 → CRC 校验 → 返回 `(cmd, payload)`；超时返回 None）
    - `read_ack(cmd, timeout_ms)`：校验 ACK（True=成功 / False=失败 / None=超时）
    - `ping()` / `read_param()`（231B）/ `save()` / `motor_ctrl(targets)` / `subscribe(interval_ms)` / `unsubscribe()` / `debug_speed(enable)`
  - `parse_status_report(payload)`：0xF0 解析（56B 常规 / 72B 扩展，按长度兼容）
- `main.py`：Demo 流程（PING → READ_PARAM → 打印）

## 用到的协议命令
| CMD | 名称 | 说明 |
|:---:|------|------|
| 0x01 | PING | Demo 实际使用：连通性测试 |
| 0x10 | READ_PARAM | Demo 实际使用：读取 config_t（231B） |
| 0x20 | SAVE_EEPROM | 库已封装：RAM 写入 EEPROM |
| 0x31 | MOTOR_CTRL | 库已封装：四通道批量控制（int32 LE ×4） |
| 0x40 / 0x41 | SUBSCRIBE / UNSUBSCRIBE | 库已封装：状态上报开关 |
| 0x44 | DEBUG_SPEED | 库已封装：上报扩展模式（72B） |

## 常见问题
| 现象 | 处理 |
|------|------|
| PING 超时（None） | 检查接线 / 共地；确认已发 `/uart2 115200 0 uart`；确认波特率一致 |
| CRC 校验失败 | 计算范围是 CMD+LEN+DATA（**不含 SYNC**），多项式 0x07 初值 0 |
| READ_PARAM 收不完整 | `RXBUF` 需 ≥ 231B（默认 1024，一般无需改） |
| 读帧偶尔丢帧/错位 | 帧头必须 0xAA；`read_frame` 已做滑动窗口+CRC 容错，确认两侧均为 8N1 |
| 版本不匹配 | 固件 SW_MAJOR 需与上位机协议版本 D 一致（本协议为 D=2，固件 v1.2.0+） |
