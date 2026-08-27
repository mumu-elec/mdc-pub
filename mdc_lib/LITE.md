# mdc_lite — 极简调用库规范（LITE API）

> **定位：** **独立实现**（自带 CRC8/组帧/0xF0 解析，不依赖 `mdc_lib`），只服务于一个核心场景：
> **上位机调参、下位机执行** —— 你只需告诉上位机要发什么控制量、并从下位机拿回实时转速。
> 除此之外的文本指令、config_t 全字段读写、SBUS/检测/波特率识别等**一律不管**。
> **依赖：** 各平台实现完全独立实现（自带 CRC8/组帧/0xF0 解析，不依赖 `mdc_lib`），
> 不重复造轮子，也不改变协议字节布局。

**极简范围（只涉及这 4 条二进制命令）：**

| CMD | 名称 | 用途 |
|:---:|------|------|
| `0x31` | MOTOR_CTRL | 发送四通道控制目标（下位机执行） |
| `0x40` | SUBSCRIBE | 开启状态周期上报（收速度的前提） |
| `0x41` | UNSUBSCRIBE | 关闭状态上报（善后） |
| `0xF0` | STATUS_REPORT | 下位机主动上报；从中取出 **rpm[4]** 作为速度回调 |

**两份库（同一套极简 API，按"形态"分两个文件）：**

| 库文件 | 形态 | 内容 |
|--------|------|------|
| `mdc_lite.*` | **只管调用（send-only）** | 只打包/给出要发送的控制帧，不做任何接收解析 |
| `mdc_lite_ctrl.*` | **调用+回调接收（control + speed callback）** | 在 send-only 基础上，流式接收 `0xF0` 并把四通道转速回调给用户 |

---

## 1. 通用约定

- **字节序 / CRC / 帧格式**：与 `mdc_lib`（API.md / 协议规范.md）完全一致，此处不重复。
- **命名**：C/C++ 用 `md_lite_` 前缀，命名空间 `mdc_lite`（C++）；Python 用 `md_lite` 前缀 + `MDLite` 类。
- **不碰串口**：库只返回要发送的字节（或写入输出缓冲）并解析喂入的字节；串口收发仍由用户实现。
- **各平台签名**沿用 `mdc_lib` 的既有约定（C 输出缓冲形态 / Python 返回 bytes / C++ 返回容器），
  保证与 API.md 一致，仅函数名与关注范围收窄。

---

## 2. mdc_lite —— 只管调用（send-only）

只关心"发什么"，不解析任何回包。所有函数返回**要发送的帧字节**（或写入输出缓冲并返回长度）。

### 2.1 函数一览

| 函数（统一语义） | CMD | 说明 |
|-----------------|:---:|------|
| `md_lite_ctrl(m0,m1,m2,m3)` | 0x31 | 四通道目标值（int32 LE）。含义随通道模式：open=PWM(±1000)、speed=RPM、pos=0.1° |
| `md_lite_stop()` | 0x31 | 便捷：`ctrl(0,0,0,0)` 全零帧，用于退出/急停 |
| `md_lite_subscribe(interval_ms)` | 0x40 | 开启状态上报；interval_ms 建议 ≥20（固件钳位） |
| `md_lite_unsubscribe()` | 0x41 | 关闭状态上报 |

> C 系形态：`uint16_t md_lite_ctrl(int32_t m0,int32_t m1,int32_t m2,int32_t m3, uint8_t* out, uint16_t cap)`，
> 其余同理；cap 不足或参数非法返回 `0`。

### 2.2 验证向量（与 mdc_lib 完全一致，可直接比对）

- `md_lite_ctrl(100,-200,0,300)` 的 DATA 段 = `64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00`
- `md_lite_subscribe(50)` 帧 = `AA 40 02 32 00 <crc>`（CRC 对 `40 02 32 00` 计算）
- `md_lite_unsubscribe()` 帧 = `AA 41 00 <crc>`
- `md_lite_stop()` 帧 = `AA 31 10 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 <crc>`（DATA 16B 全零）

---

## 3. mdc_lite_ctrl —— 调用+回调接收（control + speed callback）

在 send-only 全部函数基础上，增加一个**流式状态接收器**：逐字节喂入，自动找 `0xAA` 同步、
校验 CRC，收到 `0xF0 STATUS_REPORT`（56B/72B 自动兼容）时提取 `rpm[4]` 并调用用户**速度回调**。

### 3.1 语义

- 回调签名（各平台形态）：
  - C：`typedef void (*md_lite_on_speed_t)(const int32_t rpm[4]);`
  - C++：`std::function<void(const std::array<int32_t,4>&)>` 或函数指针；若用函数指针，`MdLite::set_on_speed(cb)`
  - Python / MicroPython：`MDLite(on_speed)`，`on_speed(rpm: tuple[int,int,int,int])`
- 仅 `0xF0` 会触发回调；其他帧忽略（以便与文本回显/ACK 混流，`0xF0` 前字节按规范当噪声丢弃）。

### 3.2 API

| 函数/方法（统一语义） | 说明 |
|----------------------|------|
| `md_lite_ctrl_init(parser, on_speed_cb)`（C）/ `MDLite(...)` 构造（Py）/ `MdLite`（C++） | 注册速度回调并初始化流式解析器 |
| `md_lite_ctrl_feed(parser, byte)`（C）/ `feed(byte)`（Py/C++） | 喂一个字节；`0xF0` 完整帧到达时解析并向回调派发 |
| `md_lite_ctrl()`、`md_lite_subscribe()`、`md_lite_unsubscribe()`、`md_lite_stop()` | 与 send-only 相同（发送侧） |

> 说明：发送侧函数与 `mdc_lite`（send-only）**完全同名同语义**；`mdc_lite_ctrl` 在底层
> `#include`/`import`/复用 `mdc_lite`，仅多出"回调接收器"。因此一个工程若只需发送，可只引 `mdc_lite`；
> 若需要回读转速，引 `mdc_lite_ctrl` 即可同时获得发送与回调。

---

## 4. 例程分类结构（各平台 `examples/` 下）

例程已**合并进各平台库目录**（`mdc_lib/<平台>/examples/`），不再单独放顶层 `例程/`。
每个平台 `examples/` 下按**功能级别**分三类（命名用中文子目录，与既有中文命名风格一致）：

| 类别 | 内容 |
|------|------|
| `完整/` | 该平台完整的协议示例（文本指令 + 二进制 + 配置读写等既有全套，用完整 `mdc_lib`） |
| `极简控制/` | 只演示"发送控制帧"：`mdc_lite` send-only 最小程序（独立实现） |
| `控制+回调/` | 演示"发控制帧 + 收速度回调"：`mdc_lite_ctrl` 最小程序（独立实现） |

> ROS 包归入 `cpp/examples/ros/`；单片机工程按同思路归类，保留目录 README 的「完整/极简控制/控制+回调」导览。

---

## 5. 各平台实现要求（与 API.md §7 对齐）

| 平台 | 文件 | 说明 |
|------|------|------|
| python | `mdc_lite.py` + `mdc_lite_ctrl.py` | 纯标准库；`mdc_lite_ctrl` `import mdc_lite` |
| cpp | `mdc_lite.hpp` + `mdc_lite_ctrl.hpp` | header-only，命名空间 `mdc_lite`；`mdc_lite_ctrl.hpp` `#include "mdc_lite.hpp"` |
| stm32/hal、rp2040/c-sdk、esp32/esp-idf | `mdc_lite.h/.c` + `mdc_lite_ctrl.h/.c` | 纯 C，不 include HAL；**完全独立实现**（自带 CRC8/组帧/流式 0xF0 解析，不依赖 `mdc_lib.h/.c`，仅含 `<stdint.h>`） |
| esp32/arduino、rp2040/arduino、avr/arduino_uno、esp8266/arduino | `mdc_lite.h/.cpp` + `mdc_lite_ctrl.h/.cpp` | 纯 C++，不 include Arduino 头；**完全独立实现**（自带 CRC8/组帧/0xF0 解析，不依赖 `mdc_lib.h/.cpp`） |
| esp32/micropython、rp2040/micropython、esp8266/micropython | `mdc_lite.py` + `mdc_lite_ctrl.py` | 纯 MicroPython（零依赖），**完全独立实现**（自带 CRC8/组帧/0xF0 解析，不依赖 `mdc_lib.py`，不用 `struct`/`machine`/`math`） |
| 51/keil | `mdc_lite.h/.c` + `mdc_lite_ctrl.h/.c` | C89 兼容、英文注释；**完全独立实现**（自带 CRC8/组帧/流式 0xF0 解析，不依赖 `mdc_lib.h/.c`，大数组用 `xdata`） |

**每个平台的 mdc_lite 目录 README 至少包含：** 功能、API 速览（两类各一行示例）、
**串口接入示例**（用户实现收发，调用库打包/回调）、集成步骤、与 `mdc_lib` 的关系。

---

## 6. 一致性验证要求（每个实现交付前自检）

| 用例 | 期望 |
|------|------|
| `crc8([0x01,0x00])` | `0x15` |
| `crc8(b"123456789")` | `0xF4` |
| `mdc_lite_subscribe(50)` 帧 | `AA 40 02 32 00 <crc>` |
| `mdc_lite_ctrl(100,-200,0,300)` DATA | `64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00` |
| mdc_lite_ctrl 流式解析：垃圾字节 + `0xF0` 帧(56B/72B) | 仅 `0xF0` 触发回调，rpm 四值正确；其余帧/噪声不触发 |

---

> **返回：** [mdc_lib 总览 README](README.md)
