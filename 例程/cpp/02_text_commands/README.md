# 02_text_commands — 文本指令交互终端

## 功能

- 交互式命令行终端：从键盘读一行 → 拆分为「指令 + 参数」→ 用 mdc_lib 的 `mdc::text_build(cmd, args)` 打包（自动补 `\n`）→ 发送 → 读取并打印回显
- 内置数字快捷键菜单，常用操作一键直达：
  - `1` = `/version`（查版本）　`2` = `/check`（查实时状态）　`3` = `/status`（查全部配置）
  - `4` = `/mode 1 speed`（通道1速度闭环）　`5` = `/mode 1 open`（通道1开环）
  - `6` = `/save`（RAM 配置写入 EEPROM）　`7` = 退出
- 支持直接输入任意指令（`/help` 可列出全部 24 条指令）
- 回显读取自适应单行/多行输出（空闲 150ms 判定结束）

## 硬件与环境要求

- Motor Driver Controller + USB Type-C，CH340 驱动，**2000000-8N1**
- C++17 编译器（MSVC / MinGW / Linux g++），零第三方依赖

## 依赖与 mdc_lib

mdc_lib 是 Motor Driver Controller 的**通用调用库**：文本指令打包（`/cmd args\n`）与二进制协议打包/解析全部由库完成，**串口收发由本工程自行实现**（`serial_port.h/.cpp`）。

- **本仓库内可直接构建**：本目录 `CMakeLists.txt` 已通过 `include_directories` 指向 `../../../mdc_lib/cpp`。
- **正式工程接入**：把 `mdc_lib/cpp/mdc_lib.hpp` 复制到工程 include 目录，`#include "mdc_lib.hpp"` 即可。

## mdc_lib 调用指南

本例程用到的文本指令打包 API（均返回含结尾 `'\n'` 的 `std::string`）：

| 函数 | 返回内容 | 说明 |
|------|---------|------|
| `mdc::text_build(cmd, args)` | `/cmd args\n` | 通用构造；`args` 传 `nullptr` = 省略参数（读取模式） |
| `mdc::text_version()` | `/version\n` | 查版本 |
| `mdc::text_check()` | `/check\n` | 查实时状态 |
| `mdc::text_status()` | `/status\n` | 查全部配置 |
| `mdc::text_mode(ch, mode)` | `/mode 1 speed\n` | 设置/读取通道控制模式 |
| `mdc::text_save()` | `/save\n` | 保存到 EEPROM |

实际调用示例（本例程 main.cpp）：

```cpp
#include "mdc_lib.hpp"
#include "serial_port.h"

// ── 快捷键菜单：mdc_lib 便捷封装 ──
sendAndEcho(mdc::text_version());              // 1 → "/version\n"
sendAndEcho(mdc::text_check());                // 2 → "/check\n"
sendAndEcho(mdc::text_status());               // 3 → "/status\n"
sendAndEcho(mdc::text_mode(1, "speed"));       // 4 → "/mode 1 speed\n"
sendAndEcho(mdc::text_mode(1, "open"));        // 5 → "/mode 1 open\n"
sendAndEcho(mdc::text_save());                 // 6 → "/save\n"

// ── 任意指令：拆分为 cmd + args 后用 text_build 打包 ──
// 输入 "/mode 2 pos" → cmd="/mode" args="2 pos" → "/mode 2 pos\n"
const std::string line = "/mode 2 pos";
const size_t pos = line.find_first_of(" \t");
const std::string cmd  = line.substr(0, pos);             // "/mode"
const std::string args = line.substr(pos + 1);            // "2 pos"
std::string instr = mdc::text_build(cmd, args);           // "/mode 2 pos\n"
g_sp.write(instr);                                        // 串口发送（用户实现）

// 无参数指令（读取模式）：
instr = mdc::text_build("/priority", nullptr);            // "/priority\n"
```

## 编译方法

### Windows — MSVC（VS 开发人员命令提示符）

```bat
cl /std:c++17 /EHsc /I..\..\..\mdc_lib\cpp main.cpp serial_port.cpp /Fe:text_commands.exe
```

### Windows — MinGW / MSYS2 g++

```bat
g++ -std=c++17 -O2 -I../../../mdc_lib/cpp main.cpp serial_port.cpp -o text_commands.exe
```

### Linux — g++ 或 CMake

```bash
g++ -std=c++17 -O2 -I../../../mdc_lib/cpp main.cpp serial_port.cpp -o text_commands

# 或 CMake（include_directories 已配置，直接构建）：
cmake -B build && cmake --build build
```

## 运行方法

```bash
text_commands COM3          # Windows
text_commands /dev/ttyUSB0  # Linux
```

运行示例：

```
== 已打开 COM3 @ 2000000-8N1，输入 /help 查看全部指令 ==
> 1
>> /version
<< MDC v2.1 ...
> /mode 1 speed
>> /mode 1 speed
<< OK (RAM only)
```

### ⚠️ 实时控制与 /priority、/timeout 的关系（重要）

- **控制帧（0x30 / 0x31）受仲裁**：非优先端口只有在优先端口失联超过心跳窗口（`/timeout` 值，最小 100ms）后才能接管；优先端口恢复发帧立即夺回。
- 因此 **USB 端做实时控制时，必须先执行 `/priority 1`**（在本终端里直接输入 `/priority 1` 即可，回显 `OK (RAM only)`）。
- `/timeout` 是双重角色：既是「超过设定时间未收到控制指令 → 电机输出归零」的看门狗，也是优先级心跳窗口。
- 本终端的 `/mode` 等配置类指令不受优先级限制，随时可发；`/mode` 写入只改 RAM，断电丢失，需 `/save` 持久化。

## 代码结构

| 文件 | 说明 |
|------|------|
| `serial_port.h/.cpp` | 跨平台串口封装（串口收发，用户实现） |
| `main.cpp` | 交互循环 + 快捷键菜单 + `buildText()`（拆分行 → `mdc::text_build` 打包）+ 回显读取 |
| `CMakeLists.txt` | C++17，引用 mdc_lib（include_directories） |

## 常见问题

| 现象 | 处理 |
|------|------|
| 发控制类指令电机不动 | USB 实时控制需先 `/priority 1`；检查 `/timeout` 是否把输出归零 |
| 回显乱码 | 确认波特率 2000000-8N1 |
| 指令无响应 | 确认以 `\n` 结尾（mdc_lib 打包已自动带）；无效指令会返回 `Unknown command` |
| 想恢复遥控优先 | 发送 `/priority 0` |

> 协议细节以 [`common/协议规范.md`](../../common/协议规范.md) 为准。
