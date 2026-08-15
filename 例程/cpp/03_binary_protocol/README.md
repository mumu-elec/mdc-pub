# 03_binary_protocol — mdc_lib 二进制 API 调用指南

## 功能

本工程是 **mdc_lib 二进制 API 的完整调用示例**（协议实现全部由库完成）：

- **PING（0x01）**：`mdc::bin_ping()` 打包 → 串口发送 → `mdc::Parser` 流式收帧 → `mdc::parse_ack()` 解析 ACK 结果
- **READ_PARAM（0x10）**：`mdc::bin_read_param()` 打包发送 → 接收 231B 应答 → `mdc::parse_config()` 解析打印版本与关键字段（含位域，无需手写偏移）
- **MOTOR_CTRL（0x31）打包示例**：`mdc::bin_motor_ctrl(t0,t1,t2,t3)` 演示四通道 int32 控制帧打包（仅演示打包，不发送）
- 版本信息（config_t 头部 offset 0~10 受保护区）按字节显示，config_t 内部 CRC 用 `mdc::crc8()` 校验

> 本工程**不再包含任何协议实现文件**（原 motor_driver.hpp 已删除）：CRC8、组帧、帧解析、小端字节序处理全部调用 mdc_lib，串口收发由 `serial_port.h/.cpp` 完成。

## 硬件与环境要求

- Motor Driver Controller + USB Type-C，CH340 驱动，**2000000-8N1**
- 固件 **v1.2.0+**（协议版本 D=2，config_t=231B），与程序协议版本一致
- C++17 编译器，零第三方依赖

## 依赖与 mdc_lib

mdc_lib 是 Motor Driver Controller 的**通用调用库**：二进制协议打包（`bin_ping` / `bin_read_param` / `bin_motor_ctrl` / …）、帧解析（`parse_frame` / `Parser` 流式解析器）、字段解析（`parse_ack` / `parse_config` / `parse_status` / …）、CRC8 全部由库完成。**串口收发由本工程自行实现**（`serial_port.h/.cpp`）。

- **本仓库内可直接构建**：本目录 `CMakeLists.txt` 已通过 `include_directories` 指向 `../../../mdc_lib/cpp`。
- **正式工程接入**：把 `mdc_lib/cpp/mdc_lib.hpp` 复制到工程 include 目录，`#include "mdc_lib.hpp"` 即可（单头文件、仅标准库、无需链接）。

## mdc_lib 调用指南

本例程用到的二进制 API：

| 函数 | 说明 |
|------|------|
| `mdc::bin_ping()` | 打包 PING 帧（返回整帧字节 `std::vector<uint8_t>`） |
| `mdc::bin_read_param()` | 打包 READ_PARAM 帧（应答 config_t 231B） |
| `mdc::bin_motor_ctrl(t0,t1,t2,t3)` | 打包 MOTOR_CTRL 帧（4×int32 小端由库处理） |
| `mdc::Parser::feed(byte)` | 流式解析器：逐字节喂入，收齐一帧且 CRC 通过时返回 `{cmd, payload}` |
| `mdc::parse_ack(payload, Ack&)` | 解析 ACK 帧 DATA 段（1 字节 err），`Ack::ok()` 判断成功 |
| `mdc::parse_config(raw231, Config&)` | 解析 231B config_t 为 `mdc::Config` 结构体（含位域） |
| `mdc::crc8(data, len)` | CRC8（多项式 0x07，初值 0） |

实际调用示例（本例程 main.cpp）：

```cpp
#include "mdc_lib.hpp"
#include "serial_port.h"

SerialPort sp;
sp.open("COM3", 2000000);

// ── 1) PING：打包 → 发送 → 流式解析 ACK ──
mdc::Parser parser;
sp.write(mdc::bin_ping());                     // 发送 AA 01 00 15

uint8_t cmd;
std::vector<uint8_t> payload;
// （readFrame：从串口读字节逐字节喂 parser.feed()，收齐一帧返回）
if (readFrame(sp, parser, 1000, cmd, payload) && cmd == mdc::MD_CMD_PING) {
    mdc::Ack ack;
    if (mdc::parse_ack(payload, ack) && ack.ok())
        std::printf("PING OK\n");              // err=0x00
}

// ── 2) READ_PARAM：读取 231B 配置并解析 ──
sp.write(mdc::bin_read_param());
std::vector<uint8_t> raw;
if (readFrame(sp, parser, 1000, cmd, raw)
    && cmd == mdc::MD_CMD_READ_PARAM && raw.size() == mdc::MD_CONFIG_SIZE) {
    mdc::Config cfg;
    if (mdc::parse_config(raw, cfg)) {
        std::printf("baud_rate=%u ctrl_priority=%u\n",
                    cfg.baud_rate, cfg.ctrl_priority);
    }
}

// ── 3) MOTOR_CTRL 打包示例（不发送）：通道1=100、通道2=-200、通道4=300 ──
std::vector<uint8_t> ctrl = mdc::bin_motor_ctrl(100, -200, 0, 300);
```

## 编译方法

### Windows — MSVC（VS 开发人员命令提示符）

```bat
cl /std:c++17 /EHsc /I..\..\..\mdc_lib\cpp main.cpp serial_port.cpp /Fe:binary_protocol.exe
```

### Windows — MinGW / MSYS2 g++

```bat
g++ -std=c++17 -O2 -I../../../mdc_lib/cpp main.cpp serial_port.cpp -o binary_protocol.exe
```

### Linux — g++ 或 CMake

```bash
g++ -std=c++17 -O2 -I../../../mdc_lib/cpp main.cpp serial_port.cpp -o binary_protocol

# 或 CMake（include_directories 已配置，直接构建）：
cmake -B build && cmake --build build
```

## 运行方法

```bash
binary_protocol COM3          # Windows
binary_protocol /dev/ttyUSB0  # Linux
```

预期输出（示例）：

```
== 已打开 COM3 @ 2000000-8N1 ==
== PING (0x01) ==
  OK: 设备在线，ACK err=0x00
== READ_PARAM (0x10) ==
  收到 231 字节 (期望 231)
  前 16 字节 HEX: 00 52 44 4D 01 00 01 02 01 00 3A ...
== 版本信息 (offset 0~10) ==
  magic      [0-3]  = 00 52 44 4D (正确)
  hw_ver     [4-6]  = 1.0.1
  sw_ver     [7-8]  = 2.1
  crc        [10]   = 0x3A
  crc 校验          = 通过 (计算值 0x3A)
== 关键字段 (mdc::parse_config) ==
  baud_rate       = 2000000
  cmd_timeout_ms  = 500 ms
  ...
```

## 代码结构

| 文件 | 说明 |
|------|------|
| `serial_port.h/.cpp` | 跨平台串口封装（串口收发，用户实现） |
| `main.cpp` | mdc_lib 二进制 API 调用示例：PING + READ_PARAM 解析 + MOTOR_CTRL 打包演示 |
| `CMakeLists.txt` | C++17，引用 mdc_lib（include_directories） |

（不再有 motor_driver.hpp —— 协议实现已统一收敛到 mdc_lib。）

## 常见问题

| 现象 | 处理 |
|------|------|
| PING 失败 | 确认波特率 2000000-8N1；确认没有其它程序占用串口 |
| CRC 校验失败 | mdc_lib 已按协议实现 CRC8（多项式 0x07、初值 0、范围 CMD+LEN+DATA）；若仍失败检查固件版本与波特率 |
| READ_PARAM 返回长度不符 | 固件版本过低（< v1.2.0），config_t 不是 231B |
| 版本不匹配 | 固件 SW_MAJOR 必须与程序协议版本 D 一致 |

> 协议细节以 [`common/协议规范.md`](../../common/协议规范.md) 为准。
