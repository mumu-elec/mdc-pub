# 例程 — Motor Driver Controller 上位机例程集

> **配套硬件：** 基于 STM32F401 + TB6612 的四路带编码器直流电机驱动器
> **配套固件：** v1.2.0+（协议版本 D=2，config_t=231B）
> **通信端口：** USB 虚拟串口（CH340N，固定 **2000000-8N1**）；RC 接口（USART2，`uart` 模式下同样承载文本指令与二进制帧）
> **协议库：** 本目录所有例程均基于 **mdc_lib 通用调用库** 实现（见 [mdc_lib 调用指南](#二mdc_lib-调用指南)）

---

## 一、这是什么

本目录为电机驱动控制器收集的**上位机例程**，覆盖 Python、C++、ROS、MicroPython、单片机五大场景。每个例程独立成文件夹，自带 README 文档，可直接对照运行。

**所有例程遵循同一套设计：**
- **协议部分**由 `mdc_lib` 负责 —— 打包要发送的指令、解析收到的数据；
- **串口收发**由例程自己实现 —— 负责与设备建立连接、收发字节。

## 二、mdc_lib 调用指南

### 2.1 mdc_lib 是什么

`mdc_lib` 是 Motor Driver Controller 的**通用调用库**（位于本仓库 [`../mdc_lib/`](../mdc_lib/README.md)），按应用设备提供 12 个实现（Python / C++ / STM32 / ESP32 / RP2040 / 51 / AVR / ESP8266），全部是同一套 `md_*` API。

- **打包**：`md_bin_motor_ctrl(...)` 等函数返回**要发送的字节**；
- **解析**：`md_parse_status(...)` 等函数把收到的数据解析成结构体/字典；
- **串口由你实现**：拿到字节后自己发送，收到字节后喂给解析器。

> 完整 API 签名与字节布局见 [`../mdc_lib/API.md`](../mdc_lib/API.md)。

### 2.2 各语言引用 mdc_lib 的方式

| 语言 | 引用方式 |
|------|---------|
| Python | 复制 `mdc_lib/python/mdc_lib.py` 到工程目录，`import mdc_lib` |
| C++ | 复制 `mdc_lib/cpp/mdc_lib.hpp` 到工程，`#include "mdc_lib.hpp"`（命名空间 `mdc`） |
| MicroPython | 把 `mdc_lib/<平台>/micropython/mdc_lib.py` 上传到设备，`import mdc_lib` |
| Arduino | 把 `mdc_lib/<平台>/arduino/mdc_lib.h/.cpp` 复制到 sketch 目录，`#include "mdc_lib.h"` |
| STM32 / ESP-IDF / RP2040 SDK / 51 | 把对应 `mdc_lib/<平台>/.../mdc_lib.h/.c` 加入工程，`#include "mdc_lib.h"` |

> 本仓库内的例程已配好相对路径引用，直接在仓库内即可运行；正式工程请按上表把库文件复制进你的项目。

### 2.3 最小调用示例

**打包一条文本指令**（查版本）：
```python
cmd = mdc_lib.md_text_version()      # b"/version\n"
ser.write(cmd)
```

**打包一帧实时控制**（0x31，四通道目标值）：
```python
frame = mdc_lib.md_bin_motor_ctrl(100, -200, 0, 300)
ser.write(frame)
```

**解析收到的状态帧**（0xF0，需先订阅）：
```python
parser = mdc_lib.MDParser()
for b in ser.read(64):
    f = parser.feed(b)               # 完整帧返回 (cmd, payload)
    if f and f[0] == 0xF0:
        st = mdc_lib.md_parse_status(f[1])
        print(st.rpm)                # 四通道实时转速
```

**读/写全部配置**（config_t 231B）：
```python
ser.write(mdc_lib.md_bin_read_param())   # 发送 READ_PARAM 帧
reply = ser.read(235)                     # 收到 231B 应答帧
cfg = mdc_lib.md_parse_config(extract_payload(reply))   # 解析成字典
cfg["encoder_cpr"] = [600, 600, 600, 600]
ser.write(mdc_lib.md_bin_write_param(cfg)) # 打包 WRITE_PARAM 帧并发送
```

> C++ / MicroPython / 嵌入式调用的函数名完全一致（C 系为 `md_xxx(buf, cap)` 输出缓冲形态），详见各例程 README 的「mdc_lib 调用指南」章节与 `mdc_lib/API.md`。

## 三、两种通信方式

| 方式 | 特点 | 适用场景 |
|------|------|---------|
| **文本指令**（24 条） | 人类可读，`/version`、`/mode 1 speed`… | 配置参数、调试、手工测试 |
| **二进制帧**（0xAA 开头 + CRC8） | 紧凑高效，实时控制帧 `0x31` | 实时控制、状态订阅、程序交互 |

> 完整协议规范见 [`common/协议规范.md`](common/协议规范.md)。

## 四、目录导航

### 🐍 Python（6 个，使用 `mdc_lib/python`）
| 目录 | 场景 |
|------|------|
| [python/01_hello_serial](python/01_hello_serial/README.md) | 最小连通：发送 `/version` 读回显 |
| [python/02_text_commands](python/02_text_commands/README.md) | 文本指令交互 CLI（mdc_lib 文本指令构造） |
| [python/03_binary_protocol](python/03_binary_protocol/README.md) | mdc_lib 二进制协议调用示例（PING / 读配置 / 流式解析） |
| [python/04_motor_control](python/04_motor_control/README.md) | 实时电机控制（0x31 帧 + 斜坡 + 键盘调速） |
| [python/05_status_monitor](python/05_status_monitor/README.md) | 状态监控（订阅 0xF0 + 实时表格 / CSV 记录） |
| [python/06_config_manager](python/06_config_manager/README.md) | 配置读写（config_t 231B 全字段解析 / 修改 / 保存） |

### ➕ C++（5 个，使用 `mdc_lib/cpp`）
| 目录 | 场景 |
|------|------|
| [cpp/01_hello_serial](cpp/01_hello_serial/README.md) | 跨平台串口 + 发送 `/version` 读回显 |
| [cpp/02_text_commands](cpp/02_text_commands/README.md) | 文本指令交互终端 |
| [cpp/03_binary_protocol](cpp/03_binary_protocol/README.md) | mdc_lib 二进制协议调用示例 |
| [cpp/04_motor_control](cpp/04_motor_control/README.md) | 实时控制循环（定时发送 0x31 + 键盘调速） |
| [cpp/05_status_monitor](cpp/05_status_monitor/README.md) | 状态订阅解析与控制台打印 |

### 🤖 ROS（2 个包，节点使用 `mdc_lib/cpp`）
| 目录 | 场景 |
|------|------|
| [ros/ros2_motor_driver](ros/ros2_motor_driver/README.md) | ROS2 (Humble) 驱动包：话题 + 服务 + launch + 键盘遥控 demo |
| [ros/ros1_motor_driver](ros/ros1_motor_driver/README.md) | ROS1 (Noetic) 驱动包：功能对齐 ROS2 |

### 🐍 MicroPython（4 个，以 ESP32 为例，使用 `mdc_lib/esp32/micropython`）
| 目录 | 场景 |
|------|------|
| [micropython/01_uart_hello](micropython/01_uart_hello/README.md) | UART 最小连通（文本指令） |
| [micropython/02_binary_protocol](micropython/02_binary_protocol/README.md) | mdc_lib 二进制协议调用示例 |
| [micropython/03_motor_control](micropython/03_motor_control/README.md) | 实时电机控制（0x31 帧） |
| [micropython/04_status_monitor](micropython/04_status_monitor/README.md) | 状态订阅解析与显示 |

### 🔌 单片机（4 个，分别使用 `mdc_lib` 对应平台实现）
| 目录 | 场景 |
|------|------|
| [mcu/arduino_uno](mcu/arduino_uno/README.md) | Arduino UNO（SoftwareSerial 连 RC 口，mdc_lib/avr） |
| [mcu/stm32_hal](mcu/stm32_hal/README.md) | STM32 HAL 工程（mdc_lib/stm32/hal） |
| [mcu/esp32_arduino](mcu/esp32_arduino/README.md) | ESP32 Arduino（mdc_lib/esp32/arduino） |
| [mcu/51_mcu](mcu/51_mcu/README.md) | 51 单片机（Keil C51，mdc_lib/51/keil） |

## 五、快速开始

1. USB Type-C 连接电脑，设备管理器确认 CH340 端口号（波特率固定 **2000000-8N1**）
2. 选一个最简例程跑通：Python 用户看 `python/01_hello_serial`，C++ 用户看 `cpp/01_hello_serial`
3. 实时控制前注意两点（详见协议规范 §1）：
   - 上位机经 USB 做实时控制需先执行 `/priority 1`
   - 单片机 / MicroPython 板走 RC 口需先执行 `/uart2 <波特率> 0 uart`

## 六、常见问题

| 现象 | 处理 |
|------|------|
| 串口打不开 | 确认 CH340 驱动、端口号正确 |
| 控制帧无效 | `/priority 1`（USB 主控）或确认 `/timeout` 未归零 |
| CRC 校验失败 | mdc_lib 已内置校验；确认收到的字节未经其他程序截断 |
| 版本不匹配 | 固件 SW_MAJOR 需与上位机协议版本 D 一致 |
| 找不到 mdc_lib | 按 §2.2 把对应平台库文件复制进工程 |

---

> 协议细节与字段偏移以 [`common/协议规范.md`](common/协议规范.md) 为准；mdc_lib API 以 [`../mdc_lib/API.md`](../mdc_lib/API.md) 为准。
