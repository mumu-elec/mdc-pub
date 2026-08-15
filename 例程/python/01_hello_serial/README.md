# 01_hello_serial — 最小连通性测试

## 功能

- 列出本机全部可用串口（`serial.tools.list_ports`），自动识别 CH340（VID=0x1A86 或描述含 CH340）。
- 打开 USB 虚拟串口（固定 **2000000-8N1**），用 mdc_lib 构造 `/version` 文本指令发送并打印回显。
- 再发送 `/status` 读取配置回显并打印。
- 完善的异常处理：端口打不开 / 无回显超时给出明确提示；Ctrl+C 干净退出并关闭串口。

## 硬件与环境要求

| 项目 | 要求 |
|------|------|
| 硬件 | Motor Driver Controller（STM32F401 + TB6612），USB Type-C 数据线 |
| 驱动 | CH340N 虚拟串口驱动（设备管理器应出现 COM 端口） |
| 接线 | USB 线连接设备 Type-C 口与电脑；设备需上电 |
| 系统 | Windows / Linux / macOS 均可 |
| Python | 3.8+ |
| 依赖 | pyserial（唯一依赖） |

## 依赖与 mdc_lib

```bash
pip install pyserial        # 或 pip install -r requirements.txt
```

- **mdc_lib**：协议打包/解析统一由通用调用库 mdc_lib 完成，本脚本只负责串口收发。
  mdc_lib 已随例程内置（本目录 `mdc_lib.py`），开箱即用，直接 `import mdc_lib` 即可；
  如需更新库版本，用 `../../../mdc_lib/python/mdc_lib.py` 覆盖本目录文件。

## mdc_lib 调用指南

本例程用到的 mdc_lib API（详见 [`mdc_lib/API.md`](../../../mdc_lib/API.md) §4）：

| API | 返回 | 说明 |
|-----|------|------|
| `md_text_version()` | `b"/version\n"` | 查硬件/软件版本 |
| `md_text_build("/status")` | `b"/status\n"` | 通用文本指令构造（缺 `/` 自动补上；`args=None` 为读取模式） |

实际调用示例（串口收发由用户侧实现）：

```python
import mdc_lib, serial

ser = serial.Serial("COM5", 2000000)      # 打开 USB 虚拟串口 2000000-8N1
ser.write(mdc_lib.md_text_version())      # 发送 /version
echo = ser.read(ser.in_waiting or 1)      # 读取回显（本例程封装为 read_echo）
ser.write(mdc_lib.md_text_build("/status"))
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
| `send_and_print()` | 发送一条 mdc_lib 打包的文本指令并打印回显 |
| `main()` | 主流程：列串口 → 打开 → /version → /status → 关闭 |

## 常见问题

| 现象 | 处理 |
|------|------|
| 未发现任何串口 | 确认 USB 已连接、CH340 驱动已安装；换一根数据线（非纯充电线） |
| 串口打开失败（被占用） | 关闭其它占用端口的程序后重试 |
| 打开成功但无回显 | 确认设备已上电；波特率必须为 2000000-8N1；`--port` 是否选对端口 |
| 回显乱码 | 本工具按 UTF-8 解码，固件输出为 ASCII/UTF-8；若乱码请检查波特率 |

> 协议细节以 [`common/协议规范.md`](../../common/协议规范.md) 为准。
