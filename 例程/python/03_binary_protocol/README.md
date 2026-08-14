# 03_binary_protocol — 二进制帧协议封装库 + 演示

## 功能

- `motor_driver.py`：`class MotorDriver` 二进制帧协议封装库，覆盖协议规范 §3 全部 18 条命令：
  - `crc8()` / `build_frame()`：CRC8（多项式 0x07，初值 0，范围 CMD+LEN+DATA）与组帧；
  - `read_frame()`：滑动窗口找 `0xAA` 同步 → 按 LEN 收完 → CRC 校验（自动跳过垃圾字节与 CRC 失败帧）；
  - `read_ack()`：ACK 校验（err=0x00 成功 / 0xFF 失败），等待期间自动跳过 STATUS_REPORT 等推送帧；
  - `ping()` / `read_param()` / `write_param()` / `save()` / `load()` / `factory_reset()`；
  - `motor_raw()` / `motor_ctrl()`（0x31 四通道 int32 批量控制帧）；
  - `subscribe()` / `unsubscribe()` / `debug_sbus()` / `debug_speed()` / `reboot()` / `enter_bl()`；
  - `parse_status_report()`：0xF0 帧解析（常规 56B / 扩展 72B）。
- `demo_ping.py`：连接后依次执行 ping（打印 ACK 结果）→ read_param（打印 config_t 前 16 字节 hex + 解析受保护区 magic/硬件/固件版本）→ 收尾关闭。

## 硬件与环境要求

| 项目 | 要求 |
|------|------|
| 硬件 | Motor Driver Controller（固件 v1.2.0+，协议版本 D=2），USB Type-C 线 |
| 驱动 | CH340N 虚拟串口驱动 |
| Python | 3.8+ |
| 依赖 | pyserial（唯一依赖） |

## 依赖安装

```bash
pip install -r requirements.txt
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

预期输出：PING 成功信息；config_t 前 16 字节 hex；magic（应 = `0x4D445200`）、硬件版本 vA.B.C、固件版本 vD.E（D 即协议版本，必须与上位机一致）。

## 代码结构

| 文件 | 内容 |
|------|------|
| `motor_driver.py` | 协议常量（命令码/ACK）、`MotorDriver` 类（收发/解析/命令封装） |
| `demo_ping.py` | 演示主程序：ping → read_param → 头部解析 → 关闭 |
| `requirements.txt` | 依赖声明 |

库的使用方式：

```python
from motor_driver import MotorDriver, CMD_PING

drv = MotorDriver("COM5")
err = drv.ping()                 # 0 = 成功
cfg = drv.read_param()           # 231B config_t
drv.motor_ctrl([300, 0, 0, 0])   # 通道1 开环 PWM 300
drv.subscribe(50)                # 开启 50ms 状态上报
cmd, data = drv.read_frame(1.0)  # 读取推送帧（0xF0 STATUS_REPORT）
drv.close()
```

## 用到的协议命令（二进制）

`0x01 PING`、`0x10 READ_PARAM`、`0x11 WRITE_PARAM`、`0x12 WRITE_FIELD`、`0x20 SAVE_EEPROM`、`0x21 LOAD_EEPROM`、`0x22 FACTORY_RESET`、`0x30 MOTOR_RAW`、`0x31 MOTOR_CTRL`、`0x40 SUBSCRIBE`、`0x41 UNSUBSCRIBE`、`0x43 DEBUG_SBUS`、`0x44 DEBUG_SPEED`、`0x52 ENTER_BL`、`0x53 REBOOT`、`0xF0 STATUS_REPORT`。

> 帧格式 / CRC8 / ACK / 命令表均以 [`common/协议规范.md`](../../common/协议规范.md) §3 为准。

## 常见问题

| 现象 | 处理 |
|------|------|
| PING 超时 | 检查 USB 连接、波特率 2000000-8N1、端口未被占用 |
| magic 与 0x4D445200 不一致 | 固件 SW_MAJOR（协议版本 D）与上位机不一致，需升级/降级固件 |
| 控制帧（0x31）无效 | 先执行文本指令 `/priority 1`（USB 主控）；协议识别（/detect）期间控制帧被拒绝 |
| CRC 校验失败 | 确认 CRC8 计算范围是 CMD+LEN+DATA（不含 SYNC）、多项式 0x07、初值 0 |
| 等待 ACK 时收到大量 0xF0 帧 | 正常：库会自动跳过推送帧继续等 ACK；也可先 `unsubscribe()` |
| WRITE_FIELD 报“受保护区” | offset < 12 的 magic/版本/CRC 等字段不可写（规范 §3.3），请用 WRITE_PARAM 全量写 |

> 协议细节以 [`common/协议规范.md`](../../common/协议规范.md) 为准。
