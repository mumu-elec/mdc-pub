# 例程 — Motor Driver Controller 上位机例程集

> **配套硬件：** 基于 STM32F401 + TB6612 的四路带编码器直流电机驱动器
> **配套固件：** firmware v1.2.0+（协议版本 D=2，config_t=231B）
> **协议版本：** 布局 v2.1（24 条文本指令 + 18 条二进制命令）
> **通信端口：** USB 虚拟串口（CH340N，固定 **2000000-8N1**）；RC 接口（USART2，`uart` 模式下同样承载文本指令与二进制帧）

---

## 一、这是什么

本目录为电机驱动控制器收集的**上位机（Host）例程**，覆盖 Python、C++、ROS、MicroPython、单片机五大场景。每个例程独立成文件夹，自带 README 文档，可直接对照运行。

## 二、两种通信方式（所有例程的基础）

| 方式 | 特点 | 适用场景 |
|------|------|---------|
| **文本指令**（24 条） | 人类可读，`/version`、`/mode 1 speed`… | 配置参数、调试、手工测试 |
| **二进制帧**（0xAA 开头 + CRC8） | 紧凑高效，实时控制帧 `0x31` | 实时控制、状态订阅、程序交互 |

> 完整规范见 [`common/协议规范.md`](common/协议规范.md) —— 所有例程的唯一事实来源。

## 三、目录导航

### 🐍 Python（6 个）
| 目录 | 场景 |
|------|------|
| [python/01_hello_serial](python/01_hello_serial/README.md) | 最小连通：打开串口发 `/version` 读回显 |
| [python/02_text_commands](python/02_text_commands/README.md) | 文本指令交互 CLI（24 条指令封装 + 快捷菜单） |
| [python/03_binary_protocol](python/03_binary_protocol/README.md) | 二进制帧协议封装库（CRC8 / 组帧 / ACK 校验） |
| [python/04_motor_control](python/04_motor_control/README.md) | 实时电机控制（开环 PWM / 速度 RPM / 位置 0.1°） |
| [python/05_status_monitor](python/05_status_monitor/README.md) | 状态监控（订阅 0xF0 + 实时表格 / CSV 记录） |
| [python/06_config_manager](python/06_config_manager/README.md) | 配置读写（config_t 231B 全字段解析 / 修改 / 保存） |

### ➕ C++（5 个）
| 目录 | 场景 |
|------|------|
| [cpp/01_hello_serial](cpp/01_hello_serial/README.md) | 跨平台串口（Windows Win32 / Linux termios，零依赖） |
| [cpp/02_text_commands](cpp/02_text_commands/README.md) | 文本指令交互终端 |
| [cpp/03_binary_protocol](cpp/03_binary_protocol/README.md) | 二进制协议类库（MotorDriver C++ 类） |
| [cpp/04_motor_control](cpp/04_motor_control/README.md) | 实时控制循环（定时发送 0x31 + 键盘调速） |
| [cpp/05_status_monitor](cpp/05_status_monitor/README.md) | 状态订阅解析与控制台打印 |

### 🤖 ROS（2 个包）
| 目录 | 场景 |
|------|------|
| [ros/ros2_motor_driver](ros/ros2_motor_driver/README.md) | ROS2 (Humble) 驱动包：话题 + 服务 + launch + 键盘遥控 demo |
| [ros/ros1_motor_driver](ros/ros1_motor_driver/README.md) | ROS1 (Noetic) 驱动包：功能对齐 ROS2 |

### 🐍 MicroPython（4 个，以 ESP32 为例）
| 目录 | 场景 |
|------|------|
| [micropython/01_uart_hello](micropython/01_uart_hello/README.md) | UART 最小连通（文本指令） |
| [micropython/02_binary_protocol](micropython/02_binary_protocol/README.md) | 二进制帧协议封装 |
| [micropython/03_motor_control](micropython/03_motor_control/README.md) | 实时电机控制（0x31 帧） |
| [micropython/04_status_monitor](micropython/04_status_monitor/README.md) | 状态订阅解析与显示 |

### 🔌 单片机（4 个）
| 目录 | 场景 |
|------|------|
| [mcu/arduino_uno](mcu/arduino_uno/README.md) | Arduino UNO（SoftwareSerial 连 RC 口） |
| [mcu/stm32_hal](mcu/stm32_hal/README.md) | STM32 HAL 工程（CubeMX 集成 + 协议封装源码） |
| [mcu/esp32_arduino](mcu/esp32_arduino/README.md) | ESP32 Arduino（UART2 + 双任务） |
| [mcu/51_mcu](mcu/51_mcu/README.md) | 51 单片机（Keil C51，UART 中断收发） |

## 四、快速开始

1. USB Type-C 连接电脑，设备管理器确认 CH340 端口号（波特率固定 **2000000-8N1**）
2. 选一个最简例程跑通：Python 用户看 `python/01_hello_serial`，C++ 用户看 `cpp/01_hello_serial`
3. 实时控制前注意两点（详见协议规范 §1）：
   - 上位机经 USB 做实时控制需先执行 `/priority 1`
   - 单片机 / MicroPython 板走 RC 口需先执行 `/uart2 <波特率> 0 uart`

## 五、常见问题

| 现象 | 处理 |
|------|------|
| 串口打不开 | 确认 CH340 驱动、端口号正确 |
| 控制帧无效 | `/priority 1`（USB 主控）或确认 `/timeout` 未归零 |
| CRC 校验失败 | CRC8 计算范围为 CMD+LEN+DATA，多项式 0x07 初值 0 |
| 版本不匹配 | 固件 SW_MAJOR 需与上位机协议版本 D 一致 |

---

> 协议细节与字段偏移以 [`common/协议规范.md`](common/协议规范.md) 为准。
