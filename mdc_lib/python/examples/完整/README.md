# 完整（Python）— 协议全功能示例

> 使用 `mdc_lib`（完整版），覆盖文本指令、二进制协议、实时控制、状态监控与配置读写。

| 目录 | 场景 |
|------|------|
| [01_hello_serial](01_hello_serial/README.md) | 最小连通：发送 `/version` 读回显 |
| [02_text_commands](02_text_commands/README.md) | 文本指令交互 CLI（mdc_lib 文本指令构造） |
| [03_binary_protocol](03_binary_protocol/README.md) | mdc_lib 二进制协议调用示例（PING / 读配置 / 流式解析） |
| [04_motor_control](04_motor_control/README.md) | 实时电机控制（0x31 帧 + 斜坡 + 键盘调速） |
| [05_status_monitor](05_status_monitor/README.md) | 状态监控（订阅 0xF0 + 实时表格 / CSV 记录） |
| [06_config_manager](06_config_manager/README.md) | 配置读写（config_t 231B 全字段解析 / 修改 / 保存） |

> 若只需"发送控制帧"或"发控制帧+收速度回调"，看同级 [`../极简控制/`](../极简控制/README.md)、[`../控制+回调/`](../控制+回调/README.md)。
