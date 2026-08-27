# 完整（MicroPython）— 协议全功能示例

> 以 ESP32 为例，使用 `mdc_lib`（完整版），覆盖 UART 连通、二进制协议、实时控制与状态监控。

| 目录 | 场景 |
|------|------|
| [01_uart_hello](01_uart_hello/README.md) | UART 最小连通（文本指令） |
| [02_binary_protocol](02_binary_protocol/README.md) | mdc_lib 二进制协议调用示例 |
| [03_motor_control](03_motor_control/README.md) | 实时电机控制（0x31 帧） |
| [04_status_monitor](04_status_monitor/README.md) | 状态订阅解析与显示 |

> 若只需"发送控制帧"或"发控制帧+收速度回调"，看同级 [`../极简控制/`](../极简控制/README.md)、[`../控制+回调/`](../控制+回调/README.md)。
