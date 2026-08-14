# 01_hello_serial — 最小连通测试

## 功能

- 打开 USB 虚拟串口（固定 **2000000-8N1**，CH340N）
- 发送文本指令 `/version\n`，读取回显并打印（最多等 2 秒）
- 再发送 `/status\n`，读取多行配置回显并打印
- 通过 `SerialPort` 类演示完整的「打开 → 发送 → 带超时读取 → 关闭」流程（RAII 析构自动关闭）

## 硬件与环境要求

| 项 | 要求 |
|----|------|
| 硬件 | Motor Driver Controller 主板 + USB Type-C 数据线 |
| 驱动 | Windows 需安装 CH340 驱动（设备管理器出现 COM 口）；Linux 内核自带 `ch341` 驱动 |
| 波特率 | 固定 **2000000-8N1**，不可更改 |
| 编译器 | MSVC（VS2019+）/ MinGW g++ / Linux g++，均支持 C++17 |
| 依赖 | 零第三方库，仅 Win32 API 或 Linux termios |

## 编译方法

### Windows — MSVC（VS 开发人员命令提示符）

```bat
cl /std:c++17 /EHsc main.cpp serial_port.cpp /Fe:hello_serial.exe
```

或用 Visual Studio 新建「控制台应用」空项目，把 `main.cpp / serial_port.h / serial_port.cpp` 加入工程后直接生成。

### Windows — MinGW / MSYS2 g++

```bat
g++ -std=c++17 -O2 main.cpp serial_port.cpp -o hello_serial.exe
```

### Linux — g++ 或 CMake

```bash
g++ -std=c++17 -O2 main.cpp serial_port.cpp -o hello_serial

# 或 CMake：
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
| `main.cpp` | 演示流程：/version 与 /status 回显读取 |
| `CMakeLists.txt` | C++17，无外部依赖 |

## 用到的协议命令

| 指令 | 说明 |
|------|------|
| `/version` | 显示硬件/软件版本（系统指令） |
| `/status` | 打印全部配置参数（系统指令） |

## 常见问题

| 现象 | 处理 |
|------|------|
| 打开串口失败 | 检查 CH340 驱动、端口号；Windows COM10+ 程序已自动加 `\\.\` 前缀 |
| 无回显 | 指令必须以 `\n` 结尾（部分终端需 `\r\n`）；确认波特率 2000000 |
| Linux 权限不足 | `sudo usermod -aG dialout $USER` 后重新登录 |
| 读回显超时 | 检查是否插错 Type-C 口（应插驱动器 USB 口） |

> 协议细节以 [`common/协议规范.md`](../../common/协议规范.md) 为准。
