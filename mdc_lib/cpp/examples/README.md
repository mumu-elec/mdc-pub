# 例程目录（C++17 宿主版）

本目录按**功能级别**分三类导航。所有例程均基于本平台的调用库（`mdc_lib` / `mdc_lite` / `mdc_lite_ctrl`，header-only）——协议打包与解析由库完成，**串口收发由各自工程实现**（`serial_port.h/.cpp`）。

协议依据：[`../../协议规范.md`](../../协议规范.md)（布局 v2.x，config_t=248B）。

## 导航

| 类别 | 说明 | 子目录 |
|------|------|--------|
| [完整/](完整/) | 该平台完整协议示例：文本指令 + 二进制 PING/读写配置 + 实时控制 + 状态订阅监控 | `01_hello_serial` / `02_text_commands` / `03_binary_protocol` / `04_motor_control` / `05_status_monitor` |
| [极简控制/](极简控制/) | 只用 **mdc_lite**（send-only）发送控制帧：`ctrl()` / `stop()` / `subscribe()` / `unsubscribe()`，不解析回包，`main.cpp` 即开即用 | — |
| [控制+回调/](控制+回调/) | 用 **mdc_lite_ctrl** 发控制帧 + 流式收 `0xF0` 速度回调：`MdLite` 的 `control()` + `feed()` → `on_speed(rpm)`，`main.cpp` 即开即用 | — |

## 各库速览

| 库文件 | 职责 | 说明 |
|--------|------|------|
| `mdc_lib.hpp` | 通用调用库（API.md 完整版） | 打包（文本/二进制）+ 解析（ACK/STATUS/config）+ 流式 `Parser` |
| `mdc_lite.hpp` | 极简调用库（send-only） | 只打包 3 条命令（0x31/0x40/0x41）要发送的控制帧，返回 `std::vector<uint8_t>` |
| `mdc_lite_ctrl.hpp` | 极简调用库（调用+回调） | 在 send-only 基础上流式收 `0xF0`，把四通道 rpm 回调给用户 |

> **选择规则：** 只需发送 → `mdc_lite`；需要回读转速 → `mdc_lite_ctrl`；需要全部协议能力（文本指令 / config_t / SBUS 等）→ `mdc_lib`。

## 编译与运行

每个子目录均可独立编译，详见各自 README：

| 类别 | 编译 | 运行 |
|------|------|------|
| 完整/01~05 | `g++ -std=c++17 -O2 main.cpp serial_port.cpp -o <bin>`（02/03 见各自 README；04/05 需 `-pthread`/`Threads`） | `<bin> COM3` `<bin> /dev/ttyUSB0` |
| 极简控制 | `g++ -std=c++17 -Wall -Wextra -o lite_ctrl main.cpp` | `./lite_ctrl`（打印各控制帧字节，无需硬件） |
| 控制+回调 | `g++ -std=c++17 -Wall -Wextra -o lite_ctrl_cb main.cpp` | `./lite_ctrl_cb`（打印帧 + 模拟 0xF0 回调） |

## 目录结构

```
cpp/
├── README.md            本文档（三分类导航）
├── 完整/
│   ├── 01_hello_serial     最小连通测试（文本指令 /version /status）
│   ├── 02_text_commands    文本指令交互终端
│   ├── 03_binary_protocol  二进制 API 调用指南（PING / READ_PARAM / MOTOR_CTRL 打包）
│   ├── 04_motor_control    实时电机控制（线程周期发 0x31 + 斜坡 + 键盘调速）
│   └── 05_status_monitor   状态订阅监控（订阅 0x40 + 流式收 0xF0 解析 56B/72B）
├── 极简控制/
│   ├── main.cpp           只用 mdc_lite 发送控制帧（send-only）
│   ├── README.md          功能 / API 速览 / 串口接入 / 集成步骤 / 与 mdc_lib 关系
│   └── mdc_lite.hpp / mdc_lite_ctrl.hpp / mdc_lib.hpp   （内置，与本平台一致）
└── 控制+回调/
    ├── main.cpp           用 mdc_lite_ctrl 发控制 + 收 0xF0 速度回调
    ├── README.md          功能 / API 速览 / 串口接入 / 集成步骤 / 与 mdc_lib 关系
    └── mdc_lite.hpp / mdc_lite_ctrl.hpp / mdc_lib.hpp   （内置，与本平台一致）
```

> 每个例程目录各自内置一份头文件（`mdc_lib.hpp` / `mdc_lite.hpp` / `mdc_lite_ctrl.hpp`），开箱即用；更新库版本时可参考各 README 的相对路径。
