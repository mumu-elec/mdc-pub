# 02_text_commands — 文本指令交互终端

## 功能

- 交互式命令行终端：从键盘读一行 → 发送文本指令（自动补 `\n`）→ 读取并打印回显
- 内置数字快捷键菜单，常用操作一键直达：
  - `1` = `/version`（查版本）　`2` = `/check`（查实时状态）　`3` = `/status`（查全部配置）
  - `4` = `/mode 1 speed`（通道1速度闭环）　`5` = `/mode 1 open`（通道1开环）
  - `6` = `/save`（RAM 配置写入 EEPROM）　`7` = 退出
- 支持直接输入任意指令（`/help` 可列出全部 24 条指令）
- 回显读取自适应单行/多行输出（空闲 150ms 判定结束）

## 硬件与环境要求

- Motor Driver Controller + USB Type-C，CH340 驱动，**2000000-8N1**
- C++17 编译器（MSVC / MinGW / Linux g++），零第三方依赖

## 编译方法

```bat
:: Windows MSVC
cl /std:c++17 /EHsc main.cpp serial_port.cpp /Fe:text_commands.exe

:: Windows MinGW / MSYS2
g++ -std=c++17 -O2 main.cpp serial_port.cpp -o text_commands.exe
```

```bash
# Linux g++ / CMake
g++ -std=c++17 -O2 main.cpp serial_port.cpp -o text_commands
cmake -B build && cmake --build build
```

## 运行方法

```bash
text_commands COM3        # Windows
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

## 代码结构

| 文件 | 说明 |
|------|------|
| `serial_port.h/.cpp` | 跨平台串口封装（与 01 相同，本目录自带） |
| `main.cpp` | 交互循环 + 快捷键菜单 + `sendAndEcho()` 回显读取 |
| `CMakeLists.txt` | C++17，无外部依赖 |

## 用到的协议命令

| 指令 | 说明 |
|------|------|
| `/version` `/check` `/status` | 系统查询指令 |
| `/mode <ch> open\|speed\|pos` | 控制模式（带参数=写入，仅 RAM） |
| `/save` | RAM 配置写入 EEPROM（写入后需执行才持久化） |
| `/priority [0\|1]` | 控制优先级：**0=USART2 优先（默认），1=USB 优先** |
| `/timeout [ms]` | 指令超时保护（0=关闭），同时作为优先级心跳窗口（最小 100ms） |

### ⚠️ 实时控制与 /priority、/timeout 的关系（重要）

- **控制帧（0x30 / 0x31）受仲裁**：非优先端口只有在优先端口失联超过心跳窗口（`/timeout` 值，最小 100ms）后才能接管；优先端口恢复发帧立即夺回。
- 因此 **上位机（USB）做实时控制时，必须先执行 `/priority 1`**（在本终端里直接输入 `/priority 1` 即可，回显 `OK (RAM only)`）。
- `/timeout` 是双重角色：既是「超过设定时间未收到控制指令 → 电机输出归零」的看门狗，也是优先级心跳窗口。
- 本终端的 `/mode` 等配置类指令不受优先级限制，随时可发；`/mode` 写入只改 RAM，断电丢失，需 `/save` 持久化。

## 常见问题

| 现象 | 处理 |
|------|------|
| 发控制类指令电机不动 | USB 实时控制需先 `/priority 1`；检查 `/timeout` 是否把输出归零 |
| 回显乱码 | 确认波特率 2000000-8N1 |
| 指令无响应 | 确认以 `\n` 结尾（程序自动补）；无效指令会返回 `Unknown command` |
| 想恢复遥控优先 | 发送 `/priority 0` |

> 协议细节以 [`common/协议规范.md`](../../common/协议规范.md) 为准。
