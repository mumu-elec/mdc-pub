# 03_binary_protocol — mdc_lib 二进制 API 调用示例

## 功能

- 演示通用调用库 **mdc_lib 的二进制 API** 用法：帧的组帧 / CRC8 / 帧解析 / 字段解析全部由 mdc_lib 完成，脚本只负责串口收发。
- 依次执行：
  1. `md_text_version()` —— 文本指令 `/version`，打印设备硬件/软件版本回显；
  2. `md_bin_ping()` —— 发送 0x01 PING 帧，用 `MDParser` 流式解析 ACK，`md_parse_ack` 校验（err=0 成功）；
  3. `md_bin_read_param()` —— 发送 0x10 READ_PARAM，接收 231B config_t 应答，用 `md_parse_config` 解析为 dict 并打印版本区/字段。

## 硬件与环境要求

| 项目 | 要求 |
|------|------|
| 硬件 | Motor Driver Controller（固件 v1.2.0+，协议版本 D=2），USB Type-C 数据线 |
| 驱动 | CH340N 虚拟串口驱动 |
| 接线 | USB 线连接设备与电脑；设备需上电 |
| Python | 3.8+ |
| 依赖 | pyserial（唯一依赖） |

## 依赖与 mdc_lib

```bash
pip install pyserial        # 或 pip install -r requirements.txt
```

- **mdc_lib**：二进制帧的打包与解析统一由通用调用库完成（`md_bin_*` 打包函数返回整帧 bytes；`MDParser` 流式解析器自动找 `0xAA` 同步 + CRC8 校验；`md_parse_*` 解析 payload）。
  mdc_lib 已随例程内置（本目录 `mdc_lib.py`），开箱即用，直接 `import mdc_lib` 即可；如需更新库版本，用 `../../../../python/mdc_lib.py` 覆盖本目录文件。

## mdc_lib 调用指南

本例程用到的 mdc_lib API（详见 [`mdc_lib/API.md`](../../../../API.md) §3/§5/§6）：

| API | 返回 | 说明 |
|-----|------|------|
| `md_bin_ping()` | 整帧 bytes | 打包 0x01 PING 帧 |
| `md_bin_read_param()` | 整帧 bytes | 打包 0x10 READ_PARAM 帧 |
| `md_text_version()` | `b"/version\n"` | 文本指令查版本 |
| `MDParser()` / `parser.feed(byte)` | `(cmd, payload)` 或 `None` | 逐字节流式解析，自动同步 + CRC8 校验 |
| `md_parse_ack(payload)` | `Ack(cmd, err)` | 解析 ACK（err=0 成功，非 0 失败） |
| `md_parse_config(raw)` | `dict` | 解析 231B config_t → 字段字典 |

实际调用示例（串口收发由用户侧实现）：

```python
import mdc_lib, serial

ser = serial.Serial("COM5", 2000000)
parser = mdc_lib.MDParser()                     # 流式解析器

ser.write(mdc_lib.md_bin_ping())                # ① 打包 0x01 PING 并发送
for b in ser.read(64):                          # ② 接收字节喂给解析器
    r = parser.feed(b)
    if r and r[0] == mdc_lib.MD_CMD_PING:       # ③ 完整 ACK 帧
        ack = mdc_lib.md_parse_ack(r[1])        # ④ 解析 err
        print("OK" if ack.err == 0 else "FAIL")

ser.write(mdc_lib.md_bin_read_param())          # 0x10 READ_PARAM
# ... 同样用 parser.feed 收 231B 应答 ...
cfg = mdc_lib.md_parse_config(raw_231)          # 解析为 dict（键名见 API.md §6.5）
print(cfg["baud_rate"], cfg["control_mode"])
```

## 运行方法

```bash
# 自动选择第一个 CH340
python demo_ping.py

# 指定串口
python demo_ping.py --port COM5
```

参数说明：

| 参数 | 说明 |
|------|------|
| `--port` | 串口号（如 `COM5`）；缺省自动选择第一个 CH340 |

预期输出：`/version` 版本回显；`PING 成功`；`config_t 231B` 前 16 字节 hex；`md_parse_config` 解析出的关键字段（波特率、控制模式、编码器线数、PID 参数等）。

## 代码结构

| 函数/类 | 作用 |
|---------|------|
| `SerialLink.send()` | 发送 mdc_lib 打包好的帧 |
| `SerialLink.wait_frame(cmd)` | 用 `MDParser` 流式接收指定命令的帧，返回 payload |
| `SerialLink.read_echo()` | 读取文本回显（用于 /version） |
| `main()` | 三步演示：/version → PING → READ_PARAM |

## 常见问题

| 现象 | 处理 |
|------|------|
| PING 超时 | 检查 USB 连接、波特率 2000000-8N1、端口未被占用 |
| READ_PARAM 长度不符 | 固件协议版本（SW_MAJOR / 协议版本 D）必须与 PC 端一致（当前 D=2），否则 config_t 布局不同 |
| 控制帧（0x31）无效 | 先执行文本指令 `/priority 1`（USB 主控）；协议识别（/detect）期间控制帧被拒绝 |
| 等待 ACK 时收到大量 0xF0 帧 | 正常：`MDParser` 会跳过推送帧继续等目标帧；也可先发送 `md_bin_unsubscribe()` |
| 解析器长时间无输出 | 若之前订阅过状态上报，可调用 `parser.reset()` 清空内部缓冲 |

> 帧格式 / CRC8 / ACK / 命令表 / config_t 布局以 [`common/协议规范.md`](../../../../协议规范.md) §3/§5 为准。
