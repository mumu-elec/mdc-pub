# 02_text_commands — 文本指令交互工具

## 功能

- 交互式 REPL：直接输入任意文本指令（如 `/speedctrl 1 0.5 0.02 0.01`），回车即发送并打印回显；输入缺 `/` 时自动补上。
- 数字快捷菜单：一键执行常用指令（查版本 / 实时状态 / 全部配置 / 模式切换 / 保存 EEPROM / 退出），快捷键同样由 mdc_lib 文本函数构造。
- 覆盖协议规范 §2 的全部 24 条文本指令（直接输入即可）。

## 硬件与环境要求

| 项目 | 要求 |
|------|------|
| 硬件 | Motor Driver Controller，USB Type-C 数据线 |
| 驱动 | CH340N 虚拟串口驱动 |
| 接线 | USB 线连接设备与电脑；设备需上电 |
| 系统 | Windows / Linux / macOS 均可 |
| Python | 3.8+ |
| 依赖 | pyserial（唯一依赖） |

## 依赖与 mdc_lib

```bash
pip install pyserial        # 或 pip install -r requirements.txt
```

- **mdc_lib**：文本指令的打包统一由通用调用库 mdc_lib 完成（含 `\n` 结尾与 `/` 前缀补齐），本脚本只负责串口收发与交互界面。
  mdc_lib 已随例程内置（本目录 `mdc_lib.py`），开箱即用，直接 `import mdc_lib` 即可；如需更新库版本，用 `../../../../python/mdc_lib.py` 覆盖本目录文件。

## mdc_lib 调用指南

本例程用到的 mdc_lib API（详见 [`mdc_lib/API.md`](../../../../API.md) §4）：

| API | 返回 | 说明 |
|-----|------|------|
| `md_text_build(cmd, args=None)` | `b"/mode 1 speed\n"` 等 | 通用文本指令构造；缺 `/` 自动补上，`args=None` 为读取模式 |
| `md_text_version()` / `md_text_check()` / `md_text_status()` / `md_text_save()` | `b"/version\n"` 等 | 系统指令便捷封装 |
| `md_text_mode(ch, mode)` | `b"/mode 1 speed\n"` | 通道控制模式指令 |

实际调用示例（串口收发由用户侧实现）：

```python
import mdc_lib, serial

ser = serial.Serial("COM5", 2000000)
ser.write(mdc_lib.md_text_mode(1, "speed"))        # 快捷键 4：/mode 1 speed
ser.write(mdc_lib.md_text_build("/speedctrl", "1 0.5 0.02 0.01"))  # 用户输入指令
ser.write(mdc_lib.md_text_build("priority", "1"))  # 缺 "/" 自动补上 → /priority 1
```

## 运行方法

```bash
python text_commands.py                # 自动选择第一个 CH340
python text_commands.py --port COM5    # 指定串口
```

进入后：

- 输入 `1`~`6` 执行快捷菜单指令；
- 直接输入任意文本指令（如 `/speedctrl 1 0.5 0.02 0.01`、`/cpr 1 500`、`/posangle 1 1000`）发送；
- 输入 `h` 查看菜单，`q` 或 `7` 退出；Ctrl+C 也可干净退出。

## ⚠️ 实时控制注意事项（协议规范 §1）

| 项目 | 说明 |
|------|------|
| `/priority 1` | USB 主控（PC）做实时控制前必须执行，使 USB 端口获得控制仲裁优先权；否则控制帧（0x30/0x31）可能因 USART2 优先而被拒绝 |
| `/priority 0` | 默认值：USART2（遥控器）优先 |
| `/timeout <ms>` | 指令超时保护：超过设定时间未收到控制指令，电机输出自动归零（0 = 关闭）；同时作为优先级心跳窗口（最小 100ms） |
| 配置类指令 | `/mode`、`/speedctrl` 等不受优先级限制，随时可发 |
| 持久化 | 写入参数仅改 RAM，需 `/save` 才写入 EEPROM |

## 代码结构

| 函数/类 | 作用 |
|---------|------|
| `build_cmd_bytes()` | 用户输入 → `md_text_build(cmd, args)` 打包 |
| `TextTerminal.send()` | 发送一行 mdc_lib 打包的指令并打印回显 |
| `TextTerminal.read_echo()` | 读取回显直到“安静”或超时 |
| `print_menu()` | 打印快捷菜单（显示各快捷键对应的 mdc_lib 构造结果） |
| `main()` | REPL 主循环（快捷数字 + 任意指令） |

## 常见问题

| 现象 | 处理 |
|------|------|
| 输入指令无回显 | 确认设备上电、端口正确、波特率 2000000-8N1 |
| 返回 `Unknown command` | 指令拼写错误，或当前固件版本较旧（需 v1.2.0+） |
| 返回 `ERR: ch 1-4` | 通道号超出 1~4 |
| 返回 `ERR: open/speed/pos` | `/mode` 模式参数错误（只能 open/speed/pos） |
| 写入后重启丢失 | 需执行 `/save` 持久化到 EEPROM |
| 遥控/控制失效 | 检查 `/priority` 与 `/timeout` 设置（见上文） |

> 协议细节以 [`common/协议规范.md`](../../../../协议规范.md) 为准。
