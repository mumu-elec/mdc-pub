# 01_hello_serial — 最小连通测试

## 功能

- 打开 USB 虚拟串口（固定 **2000000-8N1**，CH340N）
- 用 mdc_lib 的 `mdc::text_version()` 构造 `/version\n` 发送，读取回显并打印（最多等 2 秒）
- 用 `mdc::text_build("/status", nullptr)` 构造 `/status\n` 发送，读取多行配置回显并打印
- 演示完整的「打开串口 → mdc_lib 打包 → 发送 → 带超时读取 → 关闭」流程（RAII 析构自动关闭）

## 硬件与环境要求

| 项 | 要求 |
|----|------|
| 硬件 | Motor Driver Controller 主板 + USB Type-C 数据线 |
| 驱动 | Windows 需安装 CH340 驱动（设备管理器出现 COM 口）；Linux 内核自带 `ch341` 驱动 |
| 波特率 | 固定 **2000000-8N1**，不可更改 |
| 编译器 | MSVC（VS2019+）/ MinGW g++ / Linux g++，均支持 C++17 |
| 依赖 | 串口用系统 API（Win32 / termios）；协议打包用 mdc_lib（header-only，零第三方依赖） |

## 依赖与 mdc_lib

mdc_lib 是 Motor Driver Controller 的**通用调用库**：只负责「打包要发送的数据」与「解析收到的数据」（CRC8 / 组帧 / 帧解析等协议细节全部由库完成），**串口收发由本工程自行实现**（`serial_port.h/.cpp`）。

- **开箱即用**：`mdc_lib.hpp` 已随例程内置（本目录），`#include "mdc_lib.hpp"` 直接使用，无需任何额外配置。
- **更新库版本**：如需更新，用 `../../../../cpp/mdc_lib.hpp` 覆盖本目录的 `mdc_lib.hpp` 即可（单头文件、仅标准库、无需链接）。

## mdc_lib 调用指南

本例程用到 2 个文本指令打包函数（均返回含结尾 `'\n'` 的 `std::string`，可直接写入串口）：

| 函数 | 返回内容 | 说明 |
|------|---------|------|
| `mdc::text_version()` | `/version\n` | 便捷封装：查询版本 |
| `mdc::text_build(cmd, args)` | `/cmd args\n` | 通用构造；`args` 传 `nullptr` = 省略参数（读取模式） |

实际调用示例（本例程 main.cpp）：

```cpp
#include "mdc_lib.hpp"
#include "serial_port.h"

SerialPort sp;
sp.open("COM3", 2000000);

// 发送 /version（mdc_lib 打包，含 '\n'）
const std::string cmdVersion = mdc::text_version();
sp.write(cmdVersion);                       // 串口发送（用户实现）

// 发送 /status（无参数 = 读取全部配置）
const std::string cmdStatus = mdc::text_build("/status", nullptr);
sp.write(cmdStatus);

// 接收回显（用户实现）：读到的字节原样打印即可，本例程不需要解析
```

> 全部文本指令（24 条）都可用 `mdc::text_build("/xxx", "参数")` 构造；`/version`、`/status` 等常用指令还有 `text_version()`、`text_status()` 等便捷封装。

## 编译方法

### Windows — MSVC（VS 开发人员命令提示符）

```bat
cl /std:c++17 /EHsc /I..\..\..\mdc_lib\cpp main.cpp serial_port.cpp /Fe:hello_serial.exe
```

或用 Visual Studio 新建「控制台应用」空项目，把 `main.cpp / serial_port.h / serial_port.cpp` 加入工程，并在「附加包含目录」加入 mdc_lib 头文件所在目录后直接生成。

### Windows — MinGW / MSYS2 g++

```bat
g++ -std=c++17 -O2 main.cpp serial_port.cpp -o hello_serial.exe
```

### Linux — g++ 或 CMake

```bash
g++ -std=c++17 -O2 main.cpp serial_port.cpp -o hello_serial

# 或 CMake（include_directories 已配置，直接构建）：
cmake -B build && cmake --build build
```

## 运行方法

```bash
# Windows
hello_serial COM3

# Linux（先确认用户有 dialout 组权限：sudo usermod -aG dialout $USER）
hello_serial /dev/ttyUSB0
```

预期输出（示例）：

```
== 已打开 COM3 @ 2000000-8N1 ==
>> /version
<< MDC v2.1 HW 1.0.1 ...
>> /status
<< baud_rate=115200 ...
```

## 代码结构

| 文件 | 说明 |
|------|------|
| `serial_port.h` | `SerialPort` 类声明：open / write / read(超时) / close，RAII |
| `serial_port.cpp` | Win32（CreateFile/SetCommState/ReadFile/WriteFile）与 Linux termios（cfsetispeed/cfsetospeed + select）双平台实现 |
| `main.cpp` | 演示流程：mdc_lib 打包 /version 与 /status，串口发送 + 回显读取 |
| `CMakeLists.txt` | C++17，引用 mdc_lib（include_directories） |

## 常见问题

| 现象 | 处理 |
|------|------|
| 打开串口失败 | 检查 CH340 驱动、端口号；Windows COM10+ 程序已自动加 `\\.\` 前缀 |
| 无回显 | 确认指令以 `\n` 结尾（mdc_lib 打包已自动带）；确认波特率 2000000 |
| Linux 权限不足 | `sudo usermod -aG dialout $USER` 后重新登录 |
| 读回显超时 | 检查是否插错 Type-C 口（应插驱动器 USB 口） |

> 协议细节以 [`common/协议规范.md`](../../../../协议规范.md) 为准。
