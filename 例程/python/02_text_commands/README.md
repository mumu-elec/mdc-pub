# 02_text_commands — 文本指令交互工具

## 功能

- 封装 `send(cmd)`（发送一行并打印回显）与 `query(cmd)`（返回完整回显文本）。
- 交互式 REPL：直接输入任意 `/xxx` 指令，回车即发送并打印回显。
- 数字快捷菜单：一键执行常用指令（查版本 / 实时状态 / 配置 / 模式切换 / 速度环参数示例 / 编码器清零 / 保存 EEPROM / 退出）。
- 覆盖协议规范 §2 的全部 24 条文本指令（直接输入即可）。

## 硬件与环境要求

| 项目 | 要求 |
|------|------|
| 硬件 | Motor Driver Controller，USB Type-C 线 |
| 驱动 | CH340N 虚拟串口驱动 |
| Python | 3.8+ |
| 依赖 | pyserial（唯一依赖） |

## 依赖安装

```bash
pip install -r requirements.txt
```

## 运行方法

```bash
python text_commands.py                # 自动选择第一个 CH340
python text_commands.py --port COM5    # 指定串口
```

进入后：

- 输入 `1`~`8` 执行快捷菜单指令；
- 直接输入任意文本指令（如 `/speedctrl 1 0.5 0.02 0.01`、`/cpr 1 500`、`/posangle 1 1000`）发送；
- 输入 `h` 查看菜单，`q` 或 `9` 退出；Ctrl+C 也可干净退出。

## ⚠️ 实时控制注意事项（协议规范 §1）

| 项目 | 说明 |
|------|------|
| `/priority 1` | 上位机（USB）做实时控制前必须执行，使 USB 端口获得控制仲裁优先权；否则控制帧（0x30/0x31）可能因 USART2 优先而被拒绝 |
| `/priority 0` | 默认值：USART2（遥控器）优先 |
| `/timeout <ms>` | 指令超时保护：超过设定时间未收到控制指令，电机输出自动归零（0 = 关闭）；同时作为优先级心跳窗口（最小 100ms） |
| 配置类指令 | `/mode`、`/speedctrl` 等不受优先级限制，随时可发 |
| 持久化 | 写入参数仅改 RAM，需 `/save` 才写入 EEPROM |

## 代码结构

| 函数/类 | 作用 |
|---------|------|
| `TextTerminal.send()` | 发送一行指令并打印回显 |
| `TextTerminal.query()` | 发送指令并返回完整回显文本 |
| `TextTerminal.read_echo()` | 读取回显直到“安静”或超时 |
| `print_menu()` | 打印快捷菜单 |
| `main()` | REPL 主循环（快捷数字 + 任意指令） |

## 用到的协议命令（文本指令）

快捷菜单涉及的指令：`/version`、`/check`、`/status`、`/mode`、`/speedctrl`、`/enczero`、`/save`；
其余指令（`/help`、`/posctrl`、`/cpr`、`/inv`、`/einv`、`/posangle`、`/filter`、`/uart2`、`/priority`、`/timeout`、`/smap`、`/rmap`、`/dmap`、`/sbusparam`、`/sbusrange`、`/load`、`/reset`、`/detect`）可直接输入使用。

文本指令统一以换行（`\n`）结尾发送；固件对 `\r` / `\n` 均识别为行尾。

## 常见问题

| 现象 | 处理 |
|------|------|
| 输入指令无回显 | 确认设备上电、端口正确、波特率 2000000-8N1 |
| 返回 `Unknown command` | 指令拼写错误，或当前固件版本较旧（需 v1.2.0+） |
| 返回 `ERR: ch 1-4` | 通道号超出 1~4 |
| 返回 `ERR: open/speed/pos` | `/mode` 模式参数错误（只能 open/speed/pos） |
| 写入后重启丢失 | 需执行 `/save` 持久化到 EEPROM |
| 遥控/上位机控制失效 | 检查 `/priority` 与 `/timeout` 设置（见上文） |

> 协议细节以 [`common/协议规范.md`](../../common/协议规范.md) 为准。
