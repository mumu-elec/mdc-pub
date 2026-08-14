# 04_motor_control — 实时电机控制

## 功能

- 启动时自动执行文本指令 `/priority 1`（USB 主控）并读取回显确认——这是 USB 上位机实时控制帧生效的前提
- 按通道设置控制模式 `/mode <ch> open|speed|pos`（四通道统一）
- **发送线程**按 `--interval-ms`（默认 50ms）周期连续发送 `MOTOR_CTRL(0x31)` 帧（m1~m4 四通道 int32 小端目标值）
- **斜坡平滑**：当前值每周期向目标逼近最多 `--ramp` 步进（默认 50），避免目标突变冲击电机
- **非阻塞键盘调速**：`+` 目标+100、`-` 目标-100、`q` 退出（退出前自动发送全零帧安全停转）
  - Windows 用 `_kbhit()/_getch()`，Linux 用 `select()` + raw 终端模式

## 硬件与环境要求

- Motor Driver Controller + USB Type-C，CH340 驱动，**2000000-8N1**
- 电机接 Motor A~D 输出、编码器接编码器接口
- **速度/位置闭环模式必须先配置编码器 CPR 与 PID 参数**，否则电机不会按预期运行：
  - `/cpr <ch> <线数>` 编码器线数；`/speedctrl`、`/posctrl` 配 PID
  - 或参考例程 03 用 READ_PARAM/WRITE_PARAM 全量读写 `config_t`
- C++17 编译器（需支持 `<thread>`；Windows 用原生线程，Linux 链接 `-pthread`）

## 编译方法

```bat
:: Windows MSVC
cl /std:c++17 /EHsc main.cpp serial_port.cpp /Fe:motor_control.exe

:: Windows MinGW / MSYS2（winpthreads 已默认链接）
g++ -std=c++17 -O2 main.cpp serial_port.cpp -o motor_control.exe
```

```bash
# Linux g++（需要 -pthread）
g++ -std=c++17 -O2 main.cpp serial_port.cpp -pthread -o motor_control

# CMake（自动链接 Threads）
cmake -B build && cmake --build build
```

## 运行方法

```bash
# 开环 PWM 控制，目标 300/1000，斜坡步进 50
motor_control --port COM3 --mode open --target 300 --interval-ms 50 --ramp 50

# 速度闭环（需先配好 CPR/PID），目标 500 RPM
motor_control --port /dev/ttyUSB0 --mode speed --target 500

# 位置闭环（0.1° 精度），目标 3600 = 360.0°
motor_control --port COM3 --mode pos --target 3600
```

运行中按 `+` / `-` 调整目标，按 `q` 退出（自动发全零帧归零）。

## 代码结构

| 文件 | 说明 |
|------|------|
| `serial_port.h/.cpp` | 跨平台串口封装（与 01 相同，本目录自带） |
| `main.cpp` | 参数解析、`/priority 1` + `/mode` 初始化、`std::thread` 发送循环（斜坡平滑）、非阻塞键盘、退出归零 |
| `CMakeLists.txt` | C++17 + `Threads::Threads` |

## 用到的协议命令

| 命令 | 说明 |
|------|------|
| `/priority 1` | 文本指令：USB 优先（控制帧仲裁前提，见 §1） |
| `/mode <ch> open\|speed\|pos` | 文本指令：设置各通道控制模式 |
| `0x31 MOTOR_CTRL` | 二进制帧：`[m1~m4: 4×int32 LE]`，按 interval 连续发送；含义 = 开环 PWM ±1000 / 速度 RPM / 位置 0.1°（±3600） |

## 常见问题

| 现象 | 处理 |
|------|------|
| 发控制帧电机不动 | 确认 `/priority 1` 回显 OK；确认 `/timeout` 未把输出归零；确认协议识别（/detect）已完成 |
| 速度/位置模式无反应 | 先配好 `/cpr` 与 `/speedctrl`/`/posctrl`（PID），否则闭环无输出 |
| 退出后电机仍转 | 程序退出前会发全零帧；若被强制结束（任务管理器/SIGKILL）则需手动发 `/mode 1 open` 或重启设备 |
| 键盘无响应（Linux） | 程序会把终端切为 raw 模式，退出时自动恢复；若被强杀可用 `reset` 恢复终端 |

> 协议细节以 [`common/协议规范.md`](../../common/协议规范.md) 为准。
