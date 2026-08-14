# 05_status_monitor — 状态订阅监控

## 功能

- 发送 `SUBSCRIBE(0x40)` 开启状态周期上报（50ms），接收线程用 `readFrame`（滑动窗口找 0xAA + 按 LEN 收齐 + CRC8 校验）持续解析
- 解析 `0xF0 STATUS_REPORT`：**按 payload 长度自动兼容常规 56B 与扩展 72B 两种帧**
  - 常规 56B：`enc[4] int32` + `tgt[4] float` + `rpm[4] int32` + `sbus_frame_cnt u32` + `sbus_ok_cnt u32`
  - 扩展 72B：在 rpm 之后多 `rpm_raw[4] int32`（滤波前原始转速，需先发 `DEBUG_SPEED(0x44)` 开启）
  - 全部小端，用 `memcpy` 到 `int32_t/float` 解析，避免结构体对齐问题
- **控制台实时表格**：`\r` + ANSI 光标上移覆盖刷新 4 通道 enc/tgt/rpm（扩展模式多一列 rpm_raw）
- `Ctrl+C` 退出：先停接收线程，再发送 `UNSUBSCRIBE(0x41)` 后关闭串口

## 硬件与环境要求

- Motor Driver Controller + USB Type-C，CH340 驱动，**2000000-8N1**
- Windows 10 1809+ / Windows Terminal（ANSI VT 支持），或任意 Linux 终端
- C++17 编译器（`<thread>`），零第三方依赖

## 编译方法

```bat
:: Windows MSVC
cl /std:c++17 /EHsc main.cpp serial_port.cpp /Fe:status_monitor.exe

:: Windows MinGW / MSYS2
g++ -std=c++17 -O2 main.cpp serial_port.cpp -o status_monitor.exe
```

```bash
# Linux g++（需要 -pthread）
g++ -std=c++17 -O2 main.cpp serial_port.cpp -pthread -o status_monitor

# CMake（自动链接 Threads）
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
| `serial_port.h/.cpp` | 跨平台串口封装（与 01 相同，本目录自带） |
| `main.cpp` | SUBSCRIBE → 接收线程（readFrame + parseStatus）→ 主线程表格渲染；Ctrl+C 处理 + UNSUBSCRIBE 退出 |
| `CMakeLists.txt` | C++17 + `Threads::Threads` |

## 用到的协议命令

| CMD | 名称 | DATA | 说明 |
|:---:|------|------|------|
| `0x40` | SUBSCRIBE | `[interval_ms:2B LE]` | 开启状态周期上报（最低 20ms，本例 50ms） |
| `0x41` | UNSUBSCRIBE | 无 | 关闭状态上报（退出时发送） |
| `0x44` | DEBUG_SPEED | `[enable:1B]` | 开启后 STATUS_REPORT 变 72B 扩展帧（本例自动兼容） |
| `0xF0` | STATUS_REPORT | MCU 主动推送 | 56B / 72B 状态帧（被动接收解析） |

## 常见问题

| 现象 | 处理 |
|------|------|
| 表格不刷新/无数据 | 确认订阅成功（应先有 ACK）；确认没有其它程序占用串口 |
| 表格乱跳 | 串口缓冲残留脏数据导致帧偏移，程序会自动滑动窗口重同步；可拔插 USB 重试 |
| Windows 下表格不覆盖 | 确认终端支持 ANSI（Windows Terminal 或 Win10 1809+ 控制台）；老控制台可改用 PowerShell/Windows Terminal |
| 想显示 rpm_raw 列 | 先发送 `DEBUG_SPEED(0x44)` 开启扩展帧，程序自动识别 72B |

> 协议细节以 [`common/协议规范.md`](../../common/协议规范.md) 为准。
