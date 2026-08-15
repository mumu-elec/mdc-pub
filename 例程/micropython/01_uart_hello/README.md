# 01 UART Hello — UART 最小连通例程（ESP32 + MicroPython，基于 mdc_lib）

## 功能
- 初始化 ESP32 的 UART2（TX=GPIO17，RX=GPIO16，波特率 115200）——**串口收发由例程自己实现**
- 调用 mdc_lib 打包文本指令并发送，读取控制板回显打印：
  - `md_text_version()` → `/version\n`：查询硬件/软件版本
  - `md_text_build("/status", None)` → `/status\n`：打印全部配置参数
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

> ⚠️ 接的是控制板 **RC 接口（USART2）**，不是 Type-C USB 口（USB 口波特率固定 2000000-8N1，只给电脑串口工具用）。

## 控制板预配置
先用 USB 线连接控制板到电脑（USB 虚拟串口参数固定 **2000000-8N1**），在串口调试工具中发送：

```
/uart2 115200 0 uart
```

将 USART2 配置为 **UART 模式**（波特率 115200、极性正常、模式 uart）。该指令设置后立即生效，无需 `/save`。

## 依赖与 mdc_lib
- 本例程只依赖 MicroPython 标准库（`machine` / `utime`）与 **mdc_lib**。
- 把 `mdc_lib/esp32/micropython/mdc_lib.py` 上传到 ESP32，与 `main.py` **同目录**（`main.py` 直接 `import mdc_lib`）。
- mdc_lib 是纯计算库（零依赖），只负责协议**打包/解析**，不碰串口；串口收发在例程里自己实现。

## mdc_lib 调用指南
本例程用到的 mdc_lib API：

| API | 返回 | 说明 |
|-----|------|------|
| `md_text_version()` | `b"/version\n"` | 打包版本查询文本指令 |
| `md_text_build(cmd, args)` | `bytes` | 通用文本指令构造器；`args=None` = 省略参数（读取模式） |

实际调用示例（节选自 `main.py`）：

```python
import mdc_lib

# 发送 /version\n
uart.write(mdc_lib.md_text_version())

# 发送 /status\n（None = 不带参数 = 读取模式）
uart.write(mdc_lib.md_text_build("/status", None))
```

## 文件上传与运行（三选一）

### 方式一：Thonny（推荐）
1. 打开 Thonny → 右下角选择解释器 MicroPython (ESP32) 及 ESP32 对应串口
2. 依次把 `mdc_lib.py`、`main.py` 保存到设备（文件名不变，**同目录**）
3. 打开 `main.py` 点击运行 ▶（或将 ESP32 复位，`main.py` 开机自动运行）

### 方式二：ampy（命令行）
```
ampy --port COM5 put mdc_lib.py
ampy --port COM5 put main.py
ampy --port COM5 reset
```

### 方式三：mpremote（命令行）
```
mpremote connect COM5 cp mdc_lib.py :
mpremote connect COM5 cp main.py :
mpremote connect COM5 reset
```

> `COM5` 换成 ESP32 在电脑上实际出现的串口号。上传/复位走 ESP32 自己的 USB 串口，与 UART2 不冲突。

## 代码结构
- `main.py`：
  - 顶部集中 UART 参数常量（`UART_ID` / `TX_PIN` / `RX_PIN` / `BAUD` / `TIMEOUT_MS` / `IDLE_MS`），按板子修改只需改这里
  - `read_all()`：带超时 + 空闲判定循环读取回显
  - `send_payload()`：发送 mdc_lib 打包好的文本行并打印回显
  - `main()`：依次用 `md_text_version()`、`md_text_build("/status", None)` 打包并发送

## 常见问题
| 现象 | 处理 |
|------|------|
| 无任何回显 | 检查三根线（含共地）；确认已发 `/uart2 115200 0 uart`；确认波特率两边一致（115200） |
| 回显乱码 | `/uart2` 的 inv 参数应为 0（正常极性）；两边波特率需一致 |
| 接在 USB 口上没反应 | RC 口与 USB 口是两回事：ESP32 必须接 **RC 接口**，且 USART2 需先配置为 uart 模式 |
| 文本指令无响应 | 指令必须以 `\n` 结尾（mdc_lib 打包的文本行已自动带 `\n`） |
| ImportError: no module named 'mdc_lib' | `mdc_lib.py` 未上传或不在 `main.py` 同目录 |
| 版本不匹配 | 固件 SW_MAJOR 需与协议版本 D 一致（文本指令建议固件 v1.2.0+） |
