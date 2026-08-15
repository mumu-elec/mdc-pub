# mdc_lib — MicroPython 实现（RP2040）

> 四路直流电机驱动器（Motor Driver Controller，STM32F401 + TB6612）通信协议库的 MicroPython 移植。
> **只负责「打包」与「解析」，串口收发由你自己实现**；纯 MicroPython **零依赖**（无 `machine` / `ustruct` / `struct` / `math` 等任何 import），在任意 MicroPython 板或 CPython 上均可直接 `import`。
> 协议依据：[`../../例程/common/协议规范.md`](../../例程/common/协议规范.md)（布局 v2.1，config_t=231B，24 条文本指令，18 条二进制命令）；API 规范：[`../../API.md`](../../API.md)。

---

## 一、文件清单

| 文件 | 说明 |
|------|------|
| `mdc_lib.py` | 全量实现：函数式 API（`md_*`）+ `MDParser` 流式解析器 + `MDC` 便捷类 |

> **三平台共用同一份代码**：`esp32/micropython`、`rp2040/micropython`、`esp8266/micropython` 下的 `mdc_lib.py` 内容完全一致（哈希一致），仅本 README 的接线/引脚不同。

## 二、快速开始

1. 把 `mdc_lib.py` 复制到 RP2040 文件系统（Thonny / ampy / WebREPL 均可）。
2. **接线**（RP2040 UART0，3.3V 逻辑电平，与控制板**共地**）：

   | RP2040 | 控制板 |
   |--------|--------|
   | GPIO0（UART0 TX） | RC 口信号（USART2 RX） |
   | GPIO1（UART0 RX） | RC 口信号（USART2 TX） |
   | GND | GND（必须共地） |

3. 控制板 USART2（RC 口）需先配置为 UART 模式（通过 USB 口或 RC 口发一次）：
   ```
   /uart2 115200 0 uart
   ```
   之后 RC 口即可收发文本指令与二进制帧，波特率与本示例的 115200 保持一致。

> 说明：控制板 **USB 口固定 2000000-8N1**（CH340N 虚拟串口），波特率不可改；RC 口波特率由 `/uart2` 决定。本示例走 RC 口（USART2），波特率 115200。RP2040 的 UART0 默认即 GPIO0/GPIO1，也可改用 UART1（GPIO4/GPIO5），只改 `UART(1, ..., tx=Pin(4), rx=Pin(5))` 即可。

## 三、串口接入示例（UART0，TX=GPIO0 / RX=GPIO1）

```python
from machine import Pin, UART
import mdc_lib

# ① 初始化 UART（用户实现收发；库本身不碰任何硬件）
uart = UART(0, baudrate=115200, tx=Pin(0), rx=Pin(1), timeout=50)

# ② 发送：库返回要发送的整帧 bytes，直接 uart.write
uart.write(mdc_lib.md_bin_ping())                       # PING 帧 AA 01 00 15
uart.write(mdc_lib.md_text_build("/uart2", "115200 0 uart"))   # 文本指令
uart.write(mdc_lib.md_text_mode(1, "speed"))            # /mode 1 speed\n

# ③ 接收：收到的字节喂给流式解析器（自动找 0xAA 同步 + CRC 校验）
parser = mdc_lib.MDParser()

def pump():
    """把 UART 缓冲里的字节全部喂给解析器；返回本批收到的帧列表。"""
    frames = []
    n = uart.any()
    if n:
        for b in uart.read(n):          # b 为 0~255 的整数
            r = parser.feed(b)          # 完整且 CRC 通过的一帧返回 (cmd, payload)
            if r:
                frames.append(r)
    return frames

# ④ 完整示例：开启状态上报 → 循环读取并解析 STATUS_REPORT
uart.write(mdc_lib.md_bin_subscribe(50))                # 每 50ms 上报一次（固件钳位 ≥20ms）
while True:
    for cmd, payload in pump():
        if cmd == 0xF0:                                 # STATUS_REPORT
            st = mdc_lib.md_parse_status(payload)       # 56B/72B 自动兼容
            print("rpm:", st.rpm, " enc:", st.enc, " tgt:", st.tgt)
        elif cmd == 0x40:                               # SUBSCRIBE 的 ACK
            ack = mdc_lib.md_parse_ack(payload)
            print("subscribe ACK:", "OK" if ack.err == 0 else "FAIL 0x%02X" % ack.err)
```

**实时控制示例**（速度环目标值，需先 `/priority 1` 让 USB/上位机优先，或保持 RC 口优先并用遥控）：

```python
# 开环：PWM 直驱（ch=0 电机 A，正转，PWM 500）
uart.write(mdc_lib.md_bin_motor_raw(0, 0, 500))

# 四通道批量控制（核心控制帧 0x31，int32 LE，需以 30/50/100ms 周期连续发送）
# 开环=PWM(±1000) / 速度=RPM / 位置=0.1°(±3600)
uart.write(mdc_lib.md_bin_motor_ctrl(100, -200, 0, 300))
```

## 四、API 速览（与 API.md 一一对应）

| 分类 | 函数 |
|------|------|
| 底层 | `md_crc8(data)`、`md_build_frame(cmd, data=b"")`、`md_parse_frame(frame)`、`MDParser(max_data=250)`（`.feed(byte)` / `.reset()`） |
| 文本指令 | `md_text_build(cmd, args=None)` + 便捷封装：`md_text_version / help / status / check / detect / save / load / reset / enczero(ch) / mode(ch, mode=None)` |
| 二进制命令（15 个，返回整帧） | `md_bin_ping / read_param / write_param(cfg) / write_field(field_id, value, value_len=None) / save / load / factory_reset / motor_raw(ch, dir_, pwm) / motor_ctrl(t0,t1,t2,t3) / subscribe(interval_ms) / unsubscribe / debug_sbus(enable) / debug_speed(enable) / enter_bl / reboot` |
| 解析 | `md_parse_ack(payload)`、`md_parse_status(payload)`、`md_parse_detect(payload)`、`md_parse_sbus(payload)`、`md_parse_config(raw)`、`md_pack_config(cfg)` |
| 便捷类 | `MDC()`：全部函数封装为静态方法 + 内置 `parser`（`mdc.feed(byte)` / `mdc.reset_parser()`） |

常量：`MD_SYNC=0xAA`、`MD_MAX_DATA=250`、`MD_CONFIG_SIZE=231`、`MD_FRAME_MAX=235`、`MD_CRC8_POLY=0x07`、`MD_ERR_OK=0x00`、`MD_ERR_FAIL=0xFF`，以及全部 `MD_CMD_*` 命令字。

### 解析结果字段说明

- `md_parse_status` → `md_status_t(enc, tgt, rpm, rpm_raw, sbus_frame_cnt, sbus_ok_cnt, extended)`（轻量类，支持 `st.rpm`、`st[2]`、`enc, tgt, ... = st` 解包）：
  - `enc[4]` int32 编码器累计脉冲；`tgt[4]` float 当前目标值；`rpm[4]` int32 滤波后转速
  - `rpm_raw[4]` int32 滤波前原始 RPM（**56B 模式恒为 0**；72B 扩展模式有效，需先 `debug_speed(True)`）
  - `sbus_frame_cnt` / `sbus_ok_cnt` uint32；`extended` 1=72B 扩展 / 0=56B 常规
- `md_parse_ack` → `Ack(cmd, err)`：err=0 成功，非 0 失败；输入 1 字节 DATA 段时 `cmd=None`（命令字由帧头提供），也可直接喂完整 ACK 帧自动取 cmd
- `md_parse_detect` → `Detect(proto, inv, baud)`（proto：0 失败 / 1 SBUS / 2 UART / 3 ELRS）
- `md_parse_sbus` → `Sbus(ch)`：ch 为 16 个通道原始值
- `md_parse_config` → **dict**，键名与 API.md §6.5 / Python 版一致（见下节）

## 五、config 读写示例（config_t 231B）

```python
# ① 读取全部配置（0x10 READ_PARAM 应答 231B payload）
uart.write(mdc_lib.md_bin_read_param())
# ... 收到 0x10 帧后：
cfg = mdc_lib.md_parse_config(payload)      # -> dict，键名与 Python 版一致

# ② 修改后再写回（0x11 WRITE_PARAM，仅 RAM，需 /save 持久化）
cfg["baud_rate"] = 115200
cfg["cmd_timeout_ms"] = 100
cfg["control_mode"] = [1, 1, 0, 0]          # CH1/2 速度闭环，CH3/4 开环
cfg["speed_kp"] = [1.5, 2.0, 0.0, 0.0]
cfg["speed_ki"] = [0.1, 0.2, 0.0, 0.0]
cfg["sbus_channel"] = [1, 2, 3, 4]          # 遥控通道映射：API 值 1~16（存 0~15）
uart.write(mdc_lib.md_bin_write_param(cfg)) # 也支持直接传 dict，内部自动打包
# ③ 持久化
uart.write(mdc_lib.md_bin_save())           # 约 190ms，随后收到 ACK
```

**config dict 键名**（与 Python 版 `md_parse_config` / `md_pack_config` 一致）：

| 组 | 键 |
|----|----|
| 通讯 | `baud_rate`(u32)、`cmd_timeout_ms`(u16)、`protocol`、`sbus_inv`、`ctrl_priority`（comm_flags 位域解码） |
| 电机位域 | `control_mode[4]`（每通道 2bit：0 开环/1 速度/2 位置）、`motor_invert[4]`（每通道 2bit：bit0 引脚反转 / bit1 编码器极性） |
| 编码器/速度环 | `encoder_cpr[4]`、`speed_period_ms[4]`、`speed_pid_type[4]`（4bit）、`speed_olim[4]`、`speed_kp/ki/kd/ilim[4]` |
| 位置环 | `pos_period_ms[4]`、`pos_pid_type[4]`（4bit）、`pos_kp/ki/kd/ilim[4]`、`pos_olim[4]`、`pos_angle_cpr[4]` |
| 滤波/遥控 | `speed_filter_type[4]`（4bit）、`speed_filter_window[4]`、`sbus_channel[4]`（1~16，存 0~15）、`rc_dir_ch[4]`（同上）、`rc_map_mode[4]`、`rc_dir_en[4]`（每通道 1bit）、`sbus_param[4]`、`sbus_range_min`、`sbus_range_max` |

- `md_pack_config` 缺省字段取中性值（数值 0，通道映射默认 CH1），可用 `md_parse_config` 的结果直接回写（**往返无损**，字节级 offset≥11 一致）
- 受保护区（offset 0~10：magic/版本/crc）打包时恒为 0，固件写入时自动还原
- 未知键抛 `KeyError`，取值越界抛 `ValueError`

## 六、常见问题

| 现象 | 处理 |
|------|------|
| RC 口连不上 | 先发 `/uart2 115200 0 uart` 把 USART2 配成 UART 模式；确认波特率一致、**共地** |
| 发控制帧电机不动 | 检查 `/priority`：RC 口默认优先（/priority 0）；USB 上位机实时控制需先 `/priority 1`；检查 `/timeout` 未超时归零 |
| 控制帧时灵时不灵 | 协议识别（`/detect` 或 FUN 键）期间控制帧被拒绝，等待识别完成 |
| 文本指令无响应 | 确认带 `\n`（本库 `md_text_*` 返回均含 `\n`）；部分终端需 `\r\n` |
| 串口有杂散字节/文本回显 | 流式解析器会自动找 0xAA 同步 + CRC 校验，混流的文本行会被当作噪声丢弃（0xAA 前字节丢弃） |
| 版本不匹配 | 固件 SW_MAJOR 必须与上位机协议版本 D 一致（当前 D=2） |

## 七、一致性验证

- 三平台 `mdc_lib.py` **内容完全一致**（SHA-256 相同）
- 已通过 `python -m py_compile`（三平台）
- 已通过 CPython 内联自测（145 项全 PASS，含与 Python 参考实现逐字节交叉验证）：
  - CRC 向量：`crc8([0x01,0x00])=0x15`、`crc8(b"123456789")=0xF4`
  - PING 帧 `AA 01 00 15`；SUBSCRIBE 帧 `AA 40 02 32 00 <crc>`
  - `motor_ctrl(100,-200,0,300)` DATA 段 `64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00`
  - config `pack→parse` 往返无损（字节级 + 字段级）；流式解析器垃圾/坏帧容错
  - f32 打包与 `struct '<f'` 逐位一致（含次正规数、平局取偶、边界值）
