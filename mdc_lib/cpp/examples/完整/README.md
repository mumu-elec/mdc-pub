# 完整（C++）— 协议全功能示例

> 使用 `mdc_lib`（完整版，返回 `std::vector<uint8_t>` / `std::string`），覆盖连通、文本指令、二进制协议、实时控制与状态监控。

| 目录 | 场景 |
|------|------|
| [01_hello_serial](01_hello_serial/README.md) | 跨平台串口 + 发送 `/version` 读回显 |
| [02_text_commands](02_text_commands/README.md) | 文本指令交互终端 |
| [03_binary_protocol](03_binary_protocol/README.md) | mdc_lib 二进制协议调用示例 |
| [04_motor_control](04_motor_control/README.md) | 实时控制循环（定时发送 0x31 + 键盘调速） |
| [05_status_monitor](05_status_monitor/README.md) | 状态订阅解析与控制台打印 |

> 若只需"发送控制帧"或"发控制帧+收速度回调"，看同级 [`../极简控制/`](../极简控制/README.md)、[`../控制+回调/`](../控制+回调/README.md)。
