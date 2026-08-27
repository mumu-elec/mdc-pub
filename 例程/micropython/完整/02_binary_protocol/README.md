# 02 二进制协议 — mdc_lib 二进制 API 调用指南（ESP32 + MicroPython）

## 功能
- **mdc_lib 二进制 API 调用指南**：完整演示「打包 → 发送 → 收帧 → 解析」调用链路
- **PING（0x01）**：`md_bin_ping()` 打包帧发送 → `MDParser` 逐字节收帧 → `md_parse_ack()` 校验 ACK，确认设备在线
- **READ_PARAM（0x10）**：`md_bin_read_param()` 打包帧发送 → 收 231B config_t 应答 → `md_parse_config()` 解析全字段，打印版本与关键配置
- 所有协议打包/解析均由 **mdc_lib** 完成；例程只负责 UART 收发

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

## 控制板预配置
先用 USB 线连接控制板到电脑（USB 虚拟串口参数固定 **2000000-8N1**），在串口调试工具中发送：

```
/uart2 115200 0 uart
```

将 USART2 配置为 **UART 模式**（波特率 115200、极性正常、模式 uart），立即生效，无需 `/save`。

## 依赖与 mdc_lib
- 本例程只依赖 MicroPython 标准库（`machine` / `utime`）与 **mdc_lib**。
- `mdc_lib.py` 已随例程内置（本目录），上传整个例程文件夹到设备即可运行（`main.py` 直接 `import mdc_lib`）；如需更新库版本，用 `../../../../mdc_lib/esp32/micropython/mdc_lib.py` 覆盖。
- mdc_lib 是纯计算库（零依赖），只负责协议**打包/解析**；串口收发在例程里自己实现。

## mdc_lib 调用指南
本例程用到的 mdc_lib API：

| API | 说明 |
|-----|------|
| `md_bin_ping()` | 打包 0x01 PING 帧（`b"\xAA\x01\x00\x15"`），无 DATA |
| `md_bin_read_param()` | 打包 0x10 READ_PARAM 帧，应答为 231B config_t |
| `MDParser.feed(byte)` | 流式解析：逐字节喂入，完整帧返回 `(cmd, payload)`，否则 `None`；自动找 `0xAA` 同步 + CRC8 校验 |
| `md_parse_ack(payload)` | 解析 ACK 的 1B DATA（err），`err=0x00` 成功 |
| `md_parse_config(payload)` | 解析 231B config_t → dict（全字段，含位域与浮点），键名见 mdc_lib API 文档 §6.5 |
| `MD_CMD_PING` / `MD_CMD_READ_PARAM` / `MD_CONFIG_SIZE` 等 | 命令字与尺寸常量 |

实际调用示例（节选自 `main.py`）：

```python
import mdc_lib

parser = mdc_lib.MDParser()

# ① PING：打包 -> 发送 -> 收帧 -> 解析 ACK
uart.write(mdc_lib.md_bin_ping())
r = wait_frame(parser, 500)          # 读 UART 字节喂 parser，返回 (cmd, payload)
cmd, payload = r                     # payload = b"\x00"（1B err）
ack = mdc_lib.md_parse_ack(payload)  # ack.err == 0x00 成功

# ② READ_PARAM：打包 -> 发送 -> 收 231B -> md_parse_config 解析
uart.write(mdc_lib.md_bin_read_param())
cmd, payload = wait_frame(parser, 1500)   # payload 为 231B config_t
cfg = mdc_lib.md_parse_config(payload)    # dict
print(cfg["baud_rate"], cfg["control_mode"], cfg["encoder_cpr"])
```

> 说明：config_t 前 11 字节（offset 0~10）为受保护区（magic / 硬件版本 / 固件版本 / reserved / crc，见《协议规范.md》§5），mdc_lib 不解析该区；本例程按规范偏移直接读取 magic 与版本号，便于核对固件/协议版本（D=SW_MAJOR）是否匹配。

## 文件上传与运行（三选一）

### 方式一：Thonny（推荐）
1. 打开 Thonny → 选择解释器 MicroPython (ESP32) 及对应串口
2. 依次把 `mdc_lib.py`、`main.py` 保存到设备（文件名不变，**同目录**）
3. 打开 `main.py` 点击运行 ▶（或将 ESP32 复位自动运行）

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

> `COM5` 换成 ESP32 实际串口号。`main.py` 会 `import mdc_lib`，**两个文件都要上传到同一目录**。

## 代码结构
- `main.py`：
  - 顶部 UART 参数常量（`UART_ID` / `TX_PIN` / `RX_PIN` / `BAUD` / 超时 / `RXBUF`）
  - `wait_frame()`：循环读 UART 字节喂给 `MDParser`，收到完整帧返回 `(cmd, payload)`
  - `read_u32_le()`：小端读 4B（仅受保护区 magic 用）
  - `main()`：PING → 校验 ACK → READ_PARAM → 打印版本与关键配置字段

## 常见问题
| 现象 | 处理 |
|------|------|
| PING 超时（None） | 检查接线 / 共地；确认已发 `/uart2 115200 0 uart`；确认波特率一致 |
| CRC 校验失败 | CRC8 计算范围是 CMD+LEN+DATA（**不含 SYNC**），多项式 0x07 初值 0；`MDParser` 会自动跳过坏帧 |
| READ_PARAM 收不完整 | `RXBUF` 需 ≥ 235B（默认 1024，一般无需改） |
| 收到"应答异常" | 帧 cmd 不是 0x10 或 payload 不是 231B：确认固件协议版本 D=2（布局 v2.1） |
| magic 与预期不符 | 固件 SW_MAJOR 需与协议版本 D 一致，否则协议不兼容 |
