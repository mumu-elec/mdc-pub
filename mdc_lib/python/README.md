# mdc_lib — Python 参考实现（宿主版）

> **定位：** Motor Driver Controller 通用调用库的 **Python 宿主实现** —— 只负责「打包要发送的数据」与「解析收到的数据」，**串口收发由用户自己实现**。
> **协议依据：** [`../例程/common/协议规范.md`](../../例程/common/协议规范.md)（布局 v2.1，config_t=231B，24 条文本指令，18 条二进制命令）
> **API 依据：** [`../API.md`](../API.md)（统一 API 规范，本实现与该文档逐一对应）
> **运行环境：** CPython 3.6+（MicroPython 请用 `esp32/micropython` 等平台实现）；**纯标准库，零第三方依赖**。

---

## 一、文件清单

| 文件 | 说明 |
|------|------|
| `mdc_lib.py` | 全量实现：函数式 API（`md_*`）+ `MDParser` 流式解析器 + `MDC` 便捷类 |
| `test_mdc_lib.py` | 自测脚本（无硬件，直接断言，覆盖 API.md §8 全部验证向量） |
| `README.md` | 本文档 |

## 二、快速开始

库本身不碰串口：拿返回的 `bytes` 自己发送，收到的字节喂给解析函数或流式解析器。
以下示例使用 `pyserial`（可选，仅示例需要）：

```python
import serial
from mdc_lib import md_bin_motor_ctrl, md_text_mode, MDParser, md_parse_status

ser = serial.Serial("COM5", 2000000)          # USB 虚拟串口 2000000-8N1

# ① 打包 → 发送
ser.write(md_text_mode(1, "speed"))           # 文本指令：/mode 1 speed\n
ser.write(md_bin_motor_ctrl(100, 0, 0, 0))    # 0x31 四通道批量控制（实时控制建议先 /priority 1）

# ② 接收 → 流式解析（自动找 0xAA 同步 + CRC 校验，容忍噪声）
parser = MDParser()
for b in ser.read(64):
    r = parser.feed(b)                        # 完整且 CRC 通过的一帧返回 (cmd, payload)
    if r and r[0] == 0xF0:                    # 0xF0 STATUS_REPORT
        st = md_parse_status(r[1])            # 56B 常规 / 72B 扩展自动兼容
        print(st.rpm, st.sbus_ok_cnt)
```

便捷类写法（功能与函数式 API 完全一致）：

```python
from mdc_lib import MDC

mdc = MDC()
ser.write(mdc.ping())                          # 0x01 PING 帧
ser.write(mdc.motor_ctrl(100, 0, 0, 0))
for b in ser.read(64):
    r = mdc.feed(b)                            # MDC 内置流式解析器
    if r:
        print(mdc.parse_ack(r[1]))             # ACK 解析
```

## 三、API 速览

### 3.1 底层

| 函数 | 说明 |
|------|------|
| `md_crc8(data) -> int` | CRC8（多项式 0x07，初值 0）。校验向量：`crc8([0x01,0x00])==0x15`、`crc8(b"123456789")==0xF4` |
| `md_build_frame(cmd, data=b"") -> bytes` | 组帧 `[0xAA][CMD][LEN][DATA][CRC8]`，CRC 范围 = CMD+LEN+DATA |
| `md_parse_frame(frame) -> Frame(cmd, payload, valid)` | 帧级解析：SYNC/长度/CRC 校验，无效帧返回 `valid=False`（不抛异常） |
| `MDParser(max_data=250)` | 流式解析器；`feed(byte) -> (cmd, payload) | None` |

### 3.2 文本指令（24 条通用构造 + 10 个便捷封装）

```python
md_text_build(cmd, args=None)      # "/mode 1 speed\n" / "/mode\n"（args=None 省略=读取模式）
md_text_version()  md_text_help()  md_text_status()  md_text_check()
md_text_detect()   md_text_save()  md_text_load()    md_text_reset()
md_text_enczero(ch)                # "/enczero 1\n"（ch=1~4）
md_text_mode(ch, mode=None)        # "/mode 1\n" 或 "/mode 1 speed\n"
```

其余指令（speedctrl/posctrl/cpr/inv/einv/posangle/filter/uart2/priority/timeout/smap/rmap/dmap/sbusparam/sbusrange）用 `md_text_build` 构造，例如：
`md_text_build("/speedctrl", "1 0.5 0.02 0.01 500 800 10 0")`、`md_text_build("/uart2", "115200 0 uart")`。

### 3.3 二进制命令（15 个打包函数，返回整帧）

| 函数 | CMD | DATA 布局 |
|------|:---:|-----------|
| `md_bin_ping()` | 0x01 | 无（验证向量：`AA 01 00 15`） |
| `md_bin_read_param()` | 0x10 | 无 |
| `md_bin_write_param(cfg)` | 0x11 | config_t 231B（cfg 可为 bytes 或 dict） |
| `md_bin_write_field(field_id, value, value_len=None)` | 0x12 | `[field_id:2B LE][value:nB]` |
| `md_bin_save()` / `md_bin_load()` / `md_bin_factory_reset()` | 0x20/21/22 | 无 |
| `md_bin_motor_raw(ch, dir, pwm)` | 0x30 | `[ch][dir][pwm:2B LE]`（ch=0~3，pwm=0~1000） |
| `md_bin_motor_ctrl(t0,t1,t2,t3)` | 0x31 | `[m1~m4:4×int32 LE]`（支持负数） |
| `md_bin_subscribe(interval_ms)` | 0x40 | `[interval_ms:2B LE]`（固件钳位 ≥20ms） |
| `md_bin_unsubscribe()` | 0x41 | 无 |
| `md_bin_debug_sbus(enable)` / `md_bin_debug_speed(enable)` | 0x43/44 | `[enable:1B]` |
| `md_bin_enter_bl()` / `md_bin_reboot()` | 0x52/53 | 无 |

### 3.4 解析（5 个）

| 函数 | 说明 |
|------|------|
| `md_parse_ack(payload) -> Ack(cmd, err)` | ACK DATA 段（1B err）；err=0 成功，非 0 失败。也接受完整 ACK 帧（自动取 cmd） |
| `md_parse_status(payload) -> md_status_t` | STATUS_REPORT，**56B/72B 按长度自动兼容**；字段：enc/tgt/rpm/rpm_raw/sbus_frame_cnt/sbus_ok_cnt/extended |
| `md_parse_detect(payload) -> Detect(proto, inv, baud)` | DETECT_REPORT；proto：0 失败 1=SBUS 2=UART 3=ELRS |
| `md_parse_sbus(payload) -> Sbus(ch)` | SBUS_DATA，16 通道 uint16 LE |
| `md_parse_config(raw) -> dict` / `md_pack_config(cfg) -> bytes` | config_t 231B 解析/打包，往返无损 |

## 四、config_t 字段键名（md_parse_config / md_pack_config）

| 键名 | 类型 | 说明 |
|------|------|------|
| `baud_rate` | int | USART2 波特率 |
| `cmd_timeout_ms` | int | 指令超时保护（ms，0=关闭） |
| `protocol` / `sbus_inv` / `ctrl_priority` | int | comm_flags 位域解码（bit0-3 / bit4 / bit5） |
| `control_mode[4]` | int×4 | 每电机 2bit：0 开环 1 速度 2 位置 |
| `motor_invert[4]` | int×4 | 每电机 2bit：bit0 引脚反转 bit1 编码器极性 |
| `encoder_cpr[4]` / `speed_period_ms[4]` | int×4 | 编码器线数 / 速度环周期 |
| `speed_pid_type[4]` | int×4 | 每电机 4bit：0 位置式 1 增量式 |
| `speed_olim[4]` | int×4 | 速度环输出限幅（PWM 0~1000） |
| `speed_kp/ki/kd/ilim[4]` | float×4 | 速度环 PID（f32） |
| `pos_period_ms[4]` / `pos_pid_type[4]` | int×4 | 位置环周期 / 类型 |
| `pos_kp/ki/kd/ilim[4]`、`pos_olim[4]` | float×4 | 位置环 PID / 输出限幅（RPM） |
| `pos_angle_cpr[4]` | int×4 | 位置环转一圈脉冲数（0=用 encoder_cpr） |
| `speed_filter_type[4]` / `speed_filter_window[4]` | int×4 | 滤波类型（0无 1滑动平均 2低通 3中值）/ 窗口 |
| `sbus_channel[4]` | int×4 | 遥控通道映射，**1~16**（存储 0~15，解析 +1） |
| `rc_dir_ch[4]` | int×4 | 方向映射通道，**1~16** |
| `rc_map_mode[4]` / `rc_dir_en[4]` | int×4 | 映射模式 / 方向使能（各 1bit） |
| `sbus_param[4]` | int×4 | 遥控行程 |
| `sbus_range_min` / `sbus_range_max` | int | 通道值上下边界 |

要点：

- `md_pack_config` 支持**部分字段**（缺省取中性值：数值 0，通道映射默认 CH1），
  因此 `md_parse_config` 的结果可直接原样回写（**往返无损**，位域/float/数组全部一致）。
- **受保护区（offset 0~10：magic/hw_ver/sw_ver/reserved/crc）打包时恒置 0**，固件写入时自动还原。
- float 经 f32 传输：字面量若无法被 f32 精确表示（如 `0.1`），解析回来是 f32 舍入值
  （如 `0.10000000149011612`）；库保证「解析→重打包」字节级稳定，不保证任意十进制小数逐位相等。

## 五、MDParser 流式解析器说明

- 逐字节喂入 `feed(byte)`，收到**完整且 CRC 通过**的一帧时返回 `(cmd, payload)`，否则返回 `None`。
- 滑动窗口找 `0xAA`；`LEN > max_data` 丢弃重扫；CRC 失败丢弃该字节继续扫描。
- 未消费的剩余字节保留在内部缓冲，适配**两帧连发 / 连续上报流**。
- 与二进制帧混流的**文本回显行会被当作噪声丢弃**（0xAA 前的字节全部丢弃）。
- 已知特性：若噪声恰好形成「0xAA + 合理 LEN」的伪帧，解析器会按该 LEN 等待（协议固有行为，
  无法与真实大帧区分）；可调用 `reset()` 手动清缓冲，或利用 `max_frame` 上限自动截断。

## 六、与 API.md 的一致性（Python 化约定）

函数命名、参数顺序、返回语义与 API.md 逐一对应，下列为 Python 化的显式约定：

| 项目 | 说明 |
|------|------|
| `md_parse_frame` | 无效帧返回 `Frame(valid=False)` 而不抛异常（与 C 版 valid 标志语义一致）；仅输入类型非法抛 ValueError |
| `md_parse_ack` | 输入 1B DATA 段时 `cmd=None`（命令字由帧头提供）；传入完整 ACK 帧（≥4B）自动取 `cmd` |
| `md_text_build` | `cmd` 缺 `/` 前缀时自动补上；`args=None` 表示省略参数（读取模式） |
| `md_bin_write_param` | `cfg` 接受 231B bytes **或**字段 dict（自动 `md_pack_config`） |
| `md_bin_write_field` | `value` 为 bytes 时 `value_len` 可省；为 int 时必须给 `value_len`（1/2/4） |
| `md_pack_config` | 支持部分字段（缺省中性值）；受保护区置 0 |
| `MDC` | 便捷类封装全部函数；额外提供 `parser` 流式解析器实例与 `reset_parser()` |
| 常量 | `MD_SYNC/MD_MAX_DATA/MD_CONFIG_SIZE/MD_FRAME_MAX/MD_CRC8_POLY/MD_CMD_*/MD_ERR_*/MD_PARSER_BUF` 与 API.md §2.5 一致 |

## 七、自测

```bash
python -m py_compile mdc_lib.py test_mdc_lib.py   # 语法检查
python test_mdc_lib.py                             # 全部断言通过则退出码 0
```

测试覆盖（对应 API.md §8）：

1. CRC8 两个官方向量（`0x15` / `0xF4`）；
2. `build_frame(0x01, b"")` == `AA 01 00 15`、`build_frame(0x40, [0x32,0x00])` == `AA 40 02 32 00 9E`；
3. `motor_ctrl(100,-200,0,300)` DATA == `64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00`；
4. `parse_status` 56B/72B 字段逐项 + 偏移抽查（rpm@32、frame_cnt@48/64、rpm_raw@48）；
5. `parse_config ↔ pack_config` 往返无损 + 位域字节级抽查（comm_flags/control_mode/motor_invert/sbus_channel/rc_dir_ch/rc_map_mode）；
6. 流式解析器：垃圾字节 + 坏 CRC 帧 + 两帧连发 → 只输出正常帧；DATA 内含 0xAA 不误同步。

## 八、常见问题

| 现象 | 原因 / 处理 |
|------|------------|
| 发控制帧电机不动 | USB 实时控制需先 `/priority 1`；检查 `/timeout` 未超时归零 |
| 收不到 STATUS_REPORT | 需先 `md_bin_subscribe(50)` 开启周期上报 |
| 0xF0 载荷是 72B 还是 56B | 取决于固件 `DEBUG_SPEED`（0x44）开关；`md_parse_status` 自动兼容 |
| 文本指令无响应 | 确认发送内容带 `\n`（库已自动附加） |
| 版本不匹配 | 上位机协议版本 D 必须与固件 SW_MAJOR 一致（当前布局 v2.1 → D=2） |
| 解析器长时间无输出 | 噪声形成伪帧导致等待（见 §五），可 `reset()` 清缓冲 |
