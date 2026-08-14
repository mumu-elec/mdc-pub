# 01 UART Hello — UART 最小连通例程（ESP32 + MicroPython）

## 功能
- 初始化 ESP32 的 UART2（TX=GPIO17，RX=GPIO16，波特率 115200）
- 发送文本指令 `/version\n`，循环读取控制板回显并打印（带超时与空闲判定）
- 再发送 `/status\n`，读取并打印控制板全部配置参数
- 用途：验证 ESP32 ↔ 控制板 RC 口（USART2）的**文本指令通道**是否打通

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

> ⚠️ 接的是控制板 **RC 接口（USART2）**，不是 Type-C USB 口（USB 口波特率固定 2000000-8N1，只给电脑上位机用）。

## 控制板预配置（必须）
先用 USB 线连接控制板（串口参数 **2000000-8N1**），在串口工具/上位机中发送：

```
/uart2 115200 0 uart
```

将 USART2 配置为 **UART 模式**（波特率 115200、极性正常、模式 uart）。该指令设置后立即生效，无需 `/save`。

## 文件上传与运行（三选一）

### 方式一：Thonny（推荐）
1. 打开 Thonny → 右下角选择解释器 MicroPython (ESP32) 及 ESP32 对应串口
2. 打开本目录 `main.py` → 文件 → 保存到设备，文件名保持 `main.py`
3. 点击运行 ▶（或将 ESP32 复位，`main.py` 开机自动运行）

### 方式二：ampy（命令行）
```
ampy --port COM5 put main.py
ampy --port COM5 reset
```

### 方式三：mpremote（命令行）
```
mpremote connect COM5 cp main.py :
mpremote connect COM5 reset
```

> `COM5` 换成 ESP32 在电脑上实际出现的串口号。上传/复位走 ESP32 自己的 USB 串口，与 UART2 不冲突。

## 代码结构
- `main.py`：
  - 顶部集中 UART 参数常量（`UART_ID` / `TX_PIN` / `RX_PIN` / `BAUD` / `TIMEOUT_MS` / `IDLE_MS`），按板子修改只需改这里
  - `read_all()`：带超时 + 空闲判定循环读取回显
  - `send_cmd()`：发送一条文本指令并打印回显
  - `main()`：依次发送 `/version`、`/status`

## 用到的协议命令
| 命令 | 说明 | 规范 |
|------|------|------|
| `/uart2 115200 0 uart` | USART2 通讯配置（前置，必做） | §2.5 |
| `/version` | 显示硬件/软件版本 | §2.2 |
| `/status` | 打印全部配置参数 | §2.2 |

## 常见问题
| 现象 | 处理 |
|------|------|
| 无任何回显 | 检查三根线（含共地）；确认已发 `/uart2 115200 0 uart`；确认波特率两边一致（115200） |
| 回显乱码 | `/uart2` 的 inv 参数应为 0（正常极性）；两边波特率需一致 |
| 接在 USB 口上没反应 | RC 口与 USB 口是两回事：MicroPython 板必须接 **RC 接口**，且 USART2 需先配置为 uart 模式 |
| 文本指令无响应 | 指令必须以 `\n` 结尾（本例程已自动补 `\n`） |
| 版本不匹配 | 固件 SW_MAJOR 需与上位机协议版本 D 一致（文本指令建议固件 v1.2.0+） |
