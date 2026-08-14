# 01_hello_serial — 最小连通性测试

## 功能

- 列出本机全部可用串口（`serial.tools.list_ports`），自动识别 CH340（VID=0x1A86 或描述含 CH340）。
- 打开 USB 虚拟串口（固定 **2000000-8N1**），发送 `/version` 读取回显并打印。
- 再发送 `/status` 读取配置回显并打印。
- 完善的异常处理：端口打不开 / 无回显超时给出明确提示；Ctrl+C 干净退出并关闭串口。

## 硬件与环境要求

| 项目 | 要求 |
|------|------|
| 硬件 | Motor Driver Controller（STM32F401 + TB6612），USB Type-C 线 |
| 驱动 | CH340N 虚拟串口驱动（设备管理器应出现 COM 端口） |
| 系统 | Windows / Linux / macOS 均可 |
| Python | 3.8+ |
| 依赖 | pyserial（唯一依赖） |

## 依赖安装

```bash
pip install -r requirements.txt
```

## 运行方法

```bash
# 自动选择第一个 CH340 串口
python hello_serial.py

# 指定串口
python hello_serial.py --port COM5

# 仅列出串口
python hello_serial.py --list
```

参数说明：

| 参数 | 说明 |
|------|------|
| `--port` | 串口号（如 `COM5` / `/dev/ttyUSB0`）；缺省自动选择第一个 CH340 |
| `--list` | 只列出可用串口后退出 |

预期输出：`/version` 返回硬件/软件版本，`/status` 返回全部配置参数（多行文本）。

## 代码结构

| 函数 | 作用 |
|------|------|
| `is_ch340()` | 判断串口是否为 CH340 |
| `pick_port()` | 解析 --port / 自动选择 CH340 |
| `read_echo()` | 读取回显直到“安静”或超时 |
| `send_and_print()` | 发送一条文本指令并打印回显 |
| `main()` | 主流程：列串口 → 打开 → /version → /status → 关闭 |

## 用到的协议命令（文本指令）

| 指令 | 说明 |
|------|------|
| `/version` | 显示硬件/软件版本（协议规范 §2.2） |
| `/status` | 打印全部配置参数（协议规范 §2.2） |

文本指令统一以换行（`\n`）结尾发送；固件对 `\r` / `\n` 均识别为行尾。

## 常见问题

| 现象 | 处理 |
|------|------|
| 未发现任何串口 | 确认 USB 已连接、CH340 驱动已安装；换一根数据线（非纯充电线） |
| 串口打开失败（PermissionError / 被占用） | 关闭串口助手、Web 上位机等占用端口的程序后重试 |
| 打开成功但无回显 | 确认设备已上电；波特率必须为 2000000-8N1；`--port` 是否选对端口 |
| 回显乱码 | 本工具按 UTF-8 解码，固件输出为 ASCII/UTF-8，一般不会乱码；若出现请检查波特率 |

> 协议细节以 [`common/协议规范.md`](../../common/协议规范.md) 为准。
