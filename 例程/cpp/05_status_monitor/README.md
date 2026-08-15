# 05_status_monitor — 状态订阅监控

## 功能

- 用 `mdc::bin_subscribe(50)` 打包并发送 `SUBSCRIBE(0x40)` 开启状态周期上报（50ms）
- 接收线程把串口字节**逐字节喂给 `mdc::Parser`** 流式解析（自动找 0xAA 同步字 + CRC 校验，文本噪声自动丢弃）
- 收到 `0xF0 STATUS_REPORT` 时用 `mdc::parse_status()` 解析：**按 payload 长度自动兼容常规 56B 与扩展 72B 两种帧**
  - 常规 56B：`enc[4] int32` + `tgt[4] float` + `rpm[4] int32` + `sbus_frame_cnt u32` + `sbus_ok_cnt u32`
  - 扩展 72B：在 rpm 之后多 `rpm_raw[4] int32`（滤波前原始转速，需先发 `DEBUG_SPEED(0x44)` 开启）
  - 字节序、帧解析、字段偏移全部由 mdc_lib 处理
- **控制台实时表格**：`\r` + ANSI 光标上移覆盖刷新 4 通道 enc/tgt/rpm（扩展模式多一列 rpm_raw）
- `Ctrl+C` 退出：先停接收线程，再发送 `mdc::bin_unsubscribe()` 后关闭串口

## 硬件与环境要求

- Motor Driver Controller + USB Type-C，CH340 驱动，**2000000-8N1**
- Windows 10 1809+ / Windows Terminal（ANSI VT 支持），或任意 Linux 终端
- C++17 编译器（`<thread>`），零第三方依赖

## 依赖与 mdc_lib

mdc_lib 是 Motor Driver Controller 的**通用调用库**：二进制命令打包（`bin_subscribe` / `bin_unsubscribe`）、流式帧解析（`Parser`）、状态帧解析（`parse_status`，56B/72B 自动兼容）全部由库完成。**串口收发由本工程自行实现**（`serial_port.h/.cpp`）。

- **本仓库内可直接构建**：本目录 `CMakeLists.txt` 已通过 `include_directories` 指向 `../../../mdc_lib/cpp`。
- **正式工程接入**：把 `mdc_lib/cpp/mdc_lib.hpp` 复制到工程 include 目录，`#include "mdc_lib.hpp"` 即可。

## mdc_lib 调用指南

本例程用到的 mdc API：

| 函数 | 说明 |
|------|------|
| `mdc::bin_subscribe(interval_ms)` | 打包 SUBSCRIBE(0x40) 帧：开启状态周期上报（固件钳位 ≥20ms） |
| `mdc::bin_unsubscribe()` | 打包 UNSUBSCRIBE(0x41) 帧：关闭状态上报 |
| `mdc::Parser::feed(byte)` | 流式解析器：逐字节喂入，收齐一帧且 CRC 通过时返回 `{cmd, payload}`（`std::optional`） |
| `mdc::parse_status(payload, Status&)` | 解析 0xF0 帧 DATA 段为 `mdc::Status`（56B/72B 按长度自动兼容） |
| `mdc::Status` | 结构体：`enc[4]` / `tgt[4]` / `rpm[4]` / `rpm_raw[4]` / `sbus_frame_cnt` / `sbus_ok_cnt` / `extended` |

实际调用示例（本例程 main.cpp）：

```cpp
#include "mdc_lib.hpp"
#include "serial_port.h"

SerialPort sp;
sp.open("COM3", 2000000);

// 1) 订阅 50ms 状态上报（mdc_lib 打包）
std::vector<uint8_t> sub = mdc::bin_subscribe(50);
sp.write(sub.data(), sub.size());

// 2) 接收线程：串口字节逐字节喂 Parser
mdc::Parser parser;
// （伪代码：收到字节时）
uint8_t byte;
auto frame = parser.feed(byte);              // 收齐一帧且 CRC 通过才返回
if (frame && frame->first == mdc::MD_CMD_STATUS_REPORT) {
    mdc::Status st;
    if (mdc::parse_status(frame->second, st)) {   // 56B/72B 自动兼容
        std::printf("enc1=%d rpm1=%d\n", st.enc[0], st.rpm[0]);
    }
}

// 3) 退出时取消订阅
std::vector<uint8_t> unsub = mdc::bin_unsubscribe();
sp.write(unsub.data(), unsub.size());
```

## 编译方法

### Windows — MSVC（VS 开发人员命令提示符）

```bat
cl /std:c++17 /EHsc /I..\..\..\mdc_lib\cpp main.cpp serial_port.cpp /Fe:status_monitor.exe
```

### Windows — MinGW / MSYS2 g++（winpthreads 已默认链接）

```bat
g++ -std=c++17 -O2 -I../../../mdc_lib/cpp main.cpp serial_port.cpp -o status_monitor.exe
```

### Linux — g++（需要 -pthread）或 CMake

```bash
g++ -std=c++17 -O2 -I../../../mdc_lib/cpp main.cpp serial_port.cpp -pthread -o status_monitor

# 或 CMake（自动链接 Threads）：
cmake -B build && cmake --build build
```

## 运行方法

```bash
status_monitor COM3          # Windows
status_monitor /dev/ttyUSB0  # Linux
```

预期输出（实时刷新的表格，`\r` 覆盖）：

```
Motor Driver Controller — 状态监控 (订阅 50ms, Ctrl+C 退出)
CH |      enc      |      tgt      |      rpm
----+---------------+---------------+--------------
 1 |         1234  |        300.0  |        +120
 2 |         -560  |       -300.0  |        -115
 ...
帧计数=100  SBUS 帧=100 校验通过=100  帧格式: 常规 56B
```

## 代码结构

| 文件 | 说明 |
|------|------|
| `serial_port.h/.cpp` | 跨平台串口封装（串口收发，用户实现） |
| `main.cpp` | `mdc::bin_subscribe(50)` → 接收线程（`mdc::Parser` 逐字节 feed + `mdc::parse_status`）→ 主线程表格渲染；Ctrl+C 处理 + `mdc::bin_unsubscribe()` 退出 |
| `CMakeLists.txt` | C++17 + `Threads::Threads`，引用 mdc_lib（include_directories） |

## 常见问题

| 现象 | 处理 |
|------|------|
| 表格不刷新/无数据 | 确认订阅成功（应先有 ACK）；确认没有其它程序占用串口 |
| 表格乱跳 | 串口缓冲残留脏数据导致帧偏移，mdc::Parser 会自动滑动窗口重同步；可拔插 USB 重试 |
| Windows 下表格不覆盖 | 确认终端支持 ANSI（Windows Terminal 或 Win10 1809+ 控制台）；老控制台可改用 PowerShell/Windows Terminal |
| 想显示 rpm_raw 列 | 先发送 `DEBUG_SPEED(0x44)` 开启扩展帧，`mdc::parse_status` 自动识别 72B |

> 协议细节以 [`common/协议规范.md`](../../common/协议规范.md) 为准。
