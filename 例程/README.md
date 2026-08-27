# 例程 — Motor Driver Controller 上位机例程集

> **配套硬件：** 基于 STM32F401 + TB6612 的四路带编码器直流电机驱动器
> **配套固件：** v1.2.0+（协议版本 D=2，config_t=231B）
> **通信端口：** USB 虚拟串口（CH340N，固定 **2000000-8N1**）；RC 接口（USART2，`uart` 模式下同样承载文本指令与二进制帧）
> **协议库：** 本目录所有例程均基于 **mdc_lib 通用调用库** 或 **mdc_lite 极简调用库** 实现（见 [mdc_lib 调用指南](#二mdc_lib-调用指南)）

---

## 一、这是什么

本目录为电机驱动控制器收集的**上位机例程**，覆盖 Python、C++、ROS、MicroPython、单片机五大场景。每个例程独立成文件夹，自带 README 文档，可直接对照运行。

**所有例程遵循同一套设计：**
- **协议部分**由库负责（`mdc_lib` 完整版 / `mdc_lite` 极简版）—— 打包要发送的指令、解析收到的数据；
- **串口收发**由例程自己实现 —— 负责与设备建立连接、收发字节。

**例程按功能级别分三类**（同一套"上位机调参、下位机执行"的分层思想）：

| 类别 | 定位 | 用的库 |
|------|------|--------|
| **完整/** | 该平台完整协议示例（文本指令 + 二进制 + 配置读写等全套） | `mdc_lib` |
| **极简控制/** | 只演示"发送控制帧"（只管调用，不看回包） | `mdc_lite`（send-only） |
| **控制+回调/** | 演示"发控制帧 + 收速度回调"（订阅 0xF0 回调四通道 rpm） | `mdc_lite_ctrl` |

---

## 二、mdc_lib / mdc_lite 调用指南

### 2.1 两个库各是什么

- **`mdc_lib`**（完整版，位于 [`../mdc_lib/`](../mdc_lib/README.md)）：协议全集 —— 文本指令 + 15 条二进制命令 + config_t 全字段读写/解析。按应用设备提供 12 个实现（Python / C++ / STM32 / ESP32 / RP2040 / 51 / AVR / ESP8266），同一套 `md_*` API。
- **`mdc_lite`**（极简版，规范见 [`../mdc_lib/LITE.md`](../mdc_lib/LITE.md)）：在 `mdc_lib` 之上的一层**极薄封装**，只服务"上位机调参、下位机执行"—— 只有 **① 打包 0x31 控制帧**（send-only）和 **② 流式接收 0xF0 并回调四通道 rpm**（`mdc_lite_ctrl`）。其他一律不管。字节布局/CRC8/帧格式与 `mdc_lib` 完全一致。

| 库 | 形态 | 内容 |
|----|------|------|
| `mdc_lite` | 只管调用 | `ctrl/stop/subscribe/unsubscribe`（返回要发送的帧） |
| `mdc_lite_ctrl` | 调用+回调 | 发送侧同上 + `MDLite(on_speed)` 流式接收 0xF0 → 回调 rpm |

### 2.2 各语言引用库的方式

**本仓库内的例程已把需要的库文件内置在各自文件夹中，开箱即用**（例程目录下的 `mdc_lib.*` 与 `mdc_lite*.*` 即为所需库文件）。

| 语言 | 例程内的库文件 |
|------|---------------|
| Python | `mdc_lib.py`（+ `mdc_lite.py` / `mdc_lite_ctrl.py`），`import mdc_lib` / `mdc_lite` |
| C++ | `mdc_lib.hpp`（+ `mdc_lite.hpp` / `mdc_lite_ctrl.hpp`），`#include "mdc_lite.hpp"`（命名空间 `mdc_lite`） |
| MicroPython | `mdc_lib.py`（+ `mdc_lite.py` / `mdc_lite_ctrl.py`），上传整个例程文件夹到设备 |
| Arduino | `mdc_lib.h` + `mdc_lib.cpp`（+ `mdc_lite*.*`），Arduino IDE 打开 `.ino` 自动编译 |
| STM32 / 51 / ESP-IDF / RP2040-C | `mdc_lib.h` + `mdc_lib.c`（+ `mdc_lite*.*`），加入工程编译 |

> **升级库版本：** 用 [`../mdc_lib/`](../mdc_lib/README.md) 中对应平台的同名文件覆盖例程内的库文件即可。

### 2.3 最小调用示例

**极简发送控制帧（mdc_lite send-only）**
```python
import mdc_lite, serial
ser = serial.Serial("COM5", 2000000)
ser.write(mdc_lite.subscribe(50))          # 先订阅（如需回读转速）
ser.write(mdc_lite.ctrl(100, -200, 0, 300))  # 0x31 四通道目标值
ser.write(mdc_lite.stop())                 # 全零急停
ser.write(mdc_lite.unsubscribe())          # 取消订阅
```

**极简发控制帧 + 收速度回调（mdc_lite_ctrl）**
```python
from mdc_lite_ctrl import MDLite
mdc = MDLite(lambda rpm: print("rpm:", rpm))
ser.write(mdc.subscribe(50))
while True:
    ser.write(mdc.ctrl(100, -200, 0, 300))
    for b in ser.read(64): mdc.feed(b)     # 0xF0 到达时自动回调 on_speed
```

**完整版 mdc_lib**（文本指令 / 读配置 / 状态解析等）用法见各「完整」例程 README 与 [`../mdc_lib/API.md`](../mdc_lib/API.md)。

---

## 三、两种通信方式

| 方式 | 特点 | 适用场景 |
|------|------|---------|
| **文本指令**（24 条） | 人类可读，`/version`、`/mode 1 speed`… | 配置参数、调试、手工测试 |
| **二进制帧**（0xAA 开头 + CRC8） | 紧凑高效，实时控制帧 `0x31` | 实时控制、状态订阅、程序交互 |

> 完整协议规范见 [`common/协议规范.md`](common/协议规范.md)。

---

## 四、目录导航

### 🐍 Python（使用 `mdc_lib/python`）
| 类别 | 目录 | 场景 |
|------|------|------|
| 完整 | [python/完整/](python/完整/README.md) | 01 连通 / 02 文本指令 / 03 二进制 / 04 实时控制 / 05 状态监控 / 06 配置读写 |
| 极简控制 | [python/极简控制/](python/极简控制/README.md) | 只用 mdc_lite 发送 0x31 控制帧 |
| 控制+回调 | [python/控制+回调/](python/控制+回调/README.md) | MDLite 订阅 0xF0，回调打印四通道 rpm |

### ➕ C++（使用 `mdc_lib/cpp`）
| 类别 | 目录 | 场景 |
|------|------|------|
| 完整 | [cpp/完整/](cpp/完整/README.md) | 01 连通 / 02 文本 / 03 二进制 / 04 实时控制 / 05 状态监控 |
| 极简控制 | [cpp/极简控制/](cpp/极简控制/README.md) | mdc_lite 发送控制帧 |
| 控制+回调 | [cpp/控制+回调/](cpp/控制+回调/README.md) | mdc_lite_ctrl 回调打印 rpm |

### 🤖 ROS（2 个包，节点使用 `mdc_lib/cpp`）
| 目录 | 场景 |
|------|------|
| [ros/ros2_motor_driver](ros/ros2_motor_driver/README.md) | ROS2 (Humble) 驱动包：话题 + 服务 + launch + 键盘遥控 demo |
| [ros/ros1_motor_driver](ros/ros1_motor_driver/README.md) | ROS1 (Noetic) 驱动包：功能对齐 ROS2 |

### 🐍 MicroPython（以 ESP32 为例，使用 `mdc_lib/esp32/micropython`）
| 类别 | 目录 | 场景 |
|------|------|------|
| 完整 | [micropython/完整/](micropython/完整/README.md) | 01 UART 连通 / 02 二进制 / 03 实时控制 / 04 状态监控 |
| 极简控制 | [micropython/极简控制/](micropython/极简控制/README.md) | mdc_lite 发送控制帧 |
| 控制+回调 | [micropython/控制+回调/](micropython/控制+回调/README.md) | MDLite 订阅 0xF0，回调打印四通道 rpm |

### 🔌 单片机（分别使用 `mdc_lib` 对应平台实现）
| 平台 | 完整 | 极简控制 | 控制+回调 |
|------|------|---------|-----------|
| STM32 HAL | [mcu/stm32_hal/完整/](mcu/stm32_hal/完整/README.md) | [mcu/stm32_hal/极简控制/](mcu/stm32_hal/极简控制/README.md) | [mcu/stm32_hal/控制+回调/](mcu/stm32_hal/控制+回调/README.md) |
| ESP32 Arduino | [mcu/esp32_arduino/完整/](mcu/esp32_arduino/完整/README.md) | [mcu/esp32_arduino/极简控制/](mcu/esp32_arduino/极简控制/README.md) | [mcu/esp32_arduino/控制+回调/](mcu/esp32_arduino/控制+回调/README.md) |
| Arduino UNO | [mcu/arduino_uno/完整/](mcu/arduino_uno/完整/README.md) | [mcu/arduino_uno/极简控制/](mcu/arduino_uno/极简控制/README.md) | [mcu/arduino_uno/控制+回调/](mcu/arduino_uno/控制+回调/README.md) |
| 51 单片机 | [mcu/51_mcu/完整/](mcu/51_mcu/完整/README.md) | [mcu/51_mcu/极简控制/](mcu/51_mcu/极简控制/README.md) | [mcu/51_mcu/控制+回调/](mcu/51_mcu/控制+回调/README.md) |
| ESP32 ESP-IDF | [mcu/esp32_esp_idf/完整/](mcu/esp32_esp_idf/完整/README.md) | [mcu/esp32_esp_idf/极简控制/](mcu/esp32_esp_idf/极简控制/README.md) | [mcu/esp32_esp_idf/控制+回调/](mcu/esp32_esp_idf/控制+回调/README.md) |
| RP2040 C SDK | [mcu/rp2040_csdk/完整/](mcu/rp2040_csdk/完整/README.md) | [mcu/rp2040_csdk/极简控制/](mcu/rp2040_csdk/极简控制/README.md) | [mcu/rp2040_csdk/控制+回调/](mcu/rp2040_csdk/控制+回调/README.md) |
| RP2040 Arduino | — | [mcu/rp2040_arduino/极简控制/](mcu/rp2040_arduino/极简控制/README.md) | [mcu/rp2040_arduino/控制+回调/](mcu/rp2040_arduino/控制+回调/README.md) |
| ESP8266 Arduino | — | [mcu/esp8266_arduino/极简控制/](mcu/esp8266_arduino/极简控制/README.md) | [mcu/esp8266_arduino/控制+回调/](mcu/esp8266_arduino/控制+回调/README.md) |

---

## 五、快速开始

1. USB Type-C 连接电脑，设备管理器确认 CH340 端口号（波特率固定 **2000000-8N1**）。
2. 先跑通**极简控制**：Python 用户看 `python/极简控制`，C++ 用户看 `cpp/极简控制`（只发送控制帧，最快跑通）。
3. 再看**控制+回调**：Python 用户看 `python/控制+回调`（发送 + 回调打印转速）。
4. 实时控制前注意两点（详见协议规范 §1）：
   - 上位机经 USB 做实时控制需先执行 `/priority 1`；
   - 单片机 / MicroPython 板走 RC 口需先执行 `/uart2 <波特率> 0 uart`。

---

## 六、常见问题

| 现象 | 处理 |
|------|------|
| 串口打不开 | 确认 CH340 驱动、端口号正确 |
| 控制帧无效 | `/priority 1`（USB 主控）或确认 `/timeout` 未归零 |
| 收不到速度回调 | 先 `subscribe(≥20ms)`；确认已回调打印机 / 回调已注册 |
| CRC 校验失败 | mdc_lib/mdc_lite 已内置校验；确认收到的字节未经其他程序截断 |
| 版本不匹配 | 固件 SW_MAJOR 需与上位机协议版本 D 一致 |
| 找不到库文件 | 例程目录下的 `mdc_lib.*` / `mdc_lite*.*` 即库文件；若被删除，从 `../mdc_lib/` 对应平台目录恢复 |

---

> 协议细节与字段偏移以 [`common/协议规范.md`](common/协议规范.md) 为准；mdc_lib API 以 [`../mdc_lib/API.md`](../mdc_lib/API.md)、mdc_lite API 以 [`../mdc_lib/LITE.md`](../mdc_lib/LITE.md) 为准。
