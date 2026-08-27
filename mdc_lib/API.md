# mdc_lib — 通用调用库 API 规范

> **库名：** mdc_lib（Motor Driver Controller Library）
> **定位：** 通用调用库 —— 只负责 **打包要发送的数据** 与 **解析收到的数据**，**串口收发由用户自己实现**。
> **协议依据：** `协议规范.md`（布局 v2.1，config_t=231B，24 条文本指令，18 条二进制命令）。
> **适用范围：** python / cpp / stm32(hal) / esp32(arduino,micropython,esp-idf) / rp2040(arduino,micropython,c-sdk) / 51(keil) / avr(arduino_uno) / esp8266(arduino,micropython)。
> **要求：** 所有平台实现**同一套 API 签名**，命名统一 `md_` 前缀。

---

## 1. 设计原则

1. **只打包 / 只解析**：库函数返回「要发送的字节」或「解析结果」，绝不直接操作串口/硬件外设。
2. **串口由用户实现**：用户拿库返回的字节自己 `Serial.write()` / `HAL_UART_Transmit()`；收到的字节喂给解析函数或流式解析器。
3. **统一签名**：所有平台同一套 `md_*` 函数名、参数顺序、返回约定。
4. **帧级 + 流式**：既提供「传完整帧/payload」的帧级解析，也提供逐字节流式解析器（自动找 0xAA 同步 + CRC 校验）。
5. **纯计算、零依赖**：嵌入式实现不依赖任何硬件库（HAL/Arduino 均可直接调用），只用标准整数类型与字节数组。

## 2. 通用约定

### 2.1 字节序
所有多字节字段**小端序（LE）**，与协议规范一致。

### 2.2 C 语言返回约定（嵌入式实现）
- **打包函数**：`uint16_t md_xxx(uint8_t* out, uint16_t cap)` —— 写入 `out` 并返回**写入字节数**；`cap` 不足或参数非法返回 `0`。
- **文本函数**：写入字符串（含结尾 `'\n'` 与 NUL），返回**不含 NUL 的字节数**；`cap` 不足返回 `0`。
- **解析函数**：`int md_parse_xxx(const uint8_t* payload, uint16_t len, md_xxx_t* out)` —— 成功返回 `1`，失败返回 `0`。

### 2.3 Python 返回约定
- 打包函数返回 `bytes`；文本函数返回 `bytes`（含 `\n`，UTF-8）。
- 解析函数输入 `bytes`，返回 `namedtuple` / `dict`；失败抛 `ValueError`。
- 便捷类 `MDC`：封装全部函数（可选，函数式 API 为准）。

### 2.4 可选参数语义（文本指令）
- 参数为 `NULL`（C）/ `None`（Python）时表示「省略，读取模式」，例如 `md_text_mode(1, NULL)` → `/mode 1\n`。

### 2.5 常量（所有平台一致）

| 常量 | 值 | 说明 |
|------|----|------|
| `MD_SYNC` | `0xAA` | 二进制帧同步字 |
| `MD_MAX_DATA` | `250` | DATA 段最大长度 |
| `MD_CONFIG_SIZE` | `231` | config_t 大小 |
| `MD_FRAME_MAX` | `235` | 最大整帧长度（4 + 231） |
| `MD_CRC8_POLY` | `0x07` | CRC8 多项式 |
| `MD_CMD_PING` | `0x01` | 二进制命令号（全表见 §5） |
| `MD_ERR_OK` | `0x00` | ACK 成功 |
| `MD_ERR_FAIL` | `0xFF` | ACK 失败 |

---

## 3. 底层：CRC / 组帧 / 帧解析

### 3.1 `md_crc8(data, len) -> uint8`
CRC8：多项式 `0x07`，初值 0，按位计算（协议规范 §3.1 参考实现逐行移植）。
校验向量：`md_crc8([0x01, 0x00]) == 0x15`；`md_crc8(b"123456789") == 0xF4`（CRC-8/ATM）。

### 3.2 `md_build_frame(cmd, data, data_len) -> 帧字节`
组帧 `[0xAA][CMD][LEN][DATA...][CRC8]`，CRC 计算范围 = **CMD+LEN+DATA**（不含 SYNC）。
验证向量：`md_build_frame(0x01, b"") == b"\xAA\x01\x00\x15"`。

### 3.3 `md_parse_frame(frame, len) -> {cmd, payload, valid}`
帧级解析：校验 SYNC 与 CRC。返回 `valid=1` + `cmd` + `payload`（指向帧内 DATA 起始，长度 = LEN）或 `valid=0`。
注意：payload 指向输入缓冲内部，调用方须在缓冲失效前消费。

### 3.4 流式解析器（可选但推荐，每平台都要实现）
```c
/* C: 状态结构体 + 三函数 */
typedef struct { uint8_t buf[MD_PARSER_BUF]; uint16_t len; } md_parser_t;
void  md_parser_init(md_parser_t* p);
/* 喂一个字节；收到完整且 CRC 通过的一帧时返回 1 并输出 cmd/payload/payload_len，否则返回 0 */
int   md_parser_feed(md_parser_t* p, uint8_t byte,
                     uint8_t* cmd, const uint8_t** payload, uint16_t* payload_len);
```
```python
# Python
class MDParser:
    def __init__(self, max_data=250): ...
    def feed(self, byte: int) -> Optional[tuple[int, bytes]]: ...  # 完整帧返回 (cmd, payload)，否则 None
```
- 行为：滑动窗口找 `0xAA`；LEN>250 丢弃重扫；CRC 失败丢弃该字节继续；`MD_PARSER_BUF` 默认 `256`（宏，可裁剪，51 等小内存平台可设小并注明 READ_PARAM 应答无法完整解析）。
- 文本回显与二进制帧混流时，文本行会被当作噪声丢弃（0xAA 前字节丢弃），文档说明。

---

## 4. 文本指令层（返回要发送的文本行，含 `'\n'`）

### 4.1 通用构造（覆盖全部 24 条指令）
```c
uint16_t md_text_build(const char* cmd, const char* args, char* out, uint16_t cap);
/* cmd="/mode", args="1 speed"  -> "/mode 1 speed\n"
 * cmd="/mode", args=NULL       -> "/mode\n"
 * 内部用 snprintf 拼接，不信任用户输入长度（cap 防护） */
```
```python
def md_text_build(cmd: str, args: str | None = None) -> bytes
```

### 4.2 便捷封装（全部平台实现，内部调用 md_text_build）

| 函数 | 输出示例 |
|------|---------|
| `md_text_version()` | `/version\n` |
| `md_text_help()` | `/help\n` |
| `md_text_status()` | `/status\n` |
| `md_text_check()` | `/check\n` |
| `md_text_detect()` | `/detect\n` |
| `md_text_save()` | `/save\n` |
| `md_text_load()` | `/load\n` |
| `md_text_reset()` | `/reset\n` |
| `md_text_enczero(ch)` | `/enczero 1\n` |
| `md_text_mode(ch, mode)` | mode=NULL→`/mode 1\n`；mode="speed"→`/mode 1 speed\n` |

其余指令（speedctrl/posctrl/cpr/inv/einv/posangle/filter/uart2/priority/timeout/smap/rmap/dmap/sbusparam/sbusrange）统一用 `md_text_build` 构造，args 示例见协议规范 §2：
```
md_text_build("/speedctrl", "1 0.5 0.02 0.01 500 800 10 0")
md_text_build("/uart2",     "115200 0 uart")
md_text_build("/sbusrange", "172 1811")
```

---

## 5. 二进制命令层（15 个打包函数，返回整帧字节）

| 函数 | CMD | DATA 布局 |
|------|:---:|-----------|
| `md_bin_ping()` | 0x01 | 无 |
| `md_bin_read_param()` | 0x10 | 无 |
| `md_bin_write_param(cfg)` | 0x11 | config_t 231B |
| `md_bin_write_field(field_id, value, value_len)` | 0x12 | `[field_id:2B LE][value:nB]` |
| `md_bin_save()` | 0x20 | 无 |
| `md_bin_load()` | 0x21 | 无 |
| `md_bin_factory_reset()` | 0x22 | 无 |
| `md_bin_motor_raw(ch, dir, pwm)` | 0x30 | `[ch:1B][dir:1B][pwm:2B LE]`（ch=0~3，dir=0 正/1 反，pwm=0~1000） |
| `md_bin_motor_ctrl(t0, t1, t2, t3)` | 0x31 | `[m1~m4:4×int32 LE]` |
| `md_bin_subscribe(interval_ms)` | 0x40 | `[interval_ms:2B LE]`（固件钳位 ≥20ms） |
| `md_bin_unsubscribe()` | 0x41 | 无 |
| `md_bin_debug_sbus(enable)` | 0x43 | `[enable:1B]` |
| `md_bin_debug_speed(enable)` | 0x44 | `[enable:1B]` |
| `md_bin_enter_bl()` | 0x52 | 无 |
| `md_bin_reboot()` | 0x53 | 无 |

验证向量：`md_bin_ping() == b"\xAA\x01\x00\x15"`；
`md_bin_motor_ctrl(100, -200, 0, 300)` 的 DATA 段 == `64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00`（LE）。

> 说明：ACK 帧（0xAA CMD 0x01 err CRC）由**解析层**识别（§6.1），打包层不负责。

---

## 6. 解析层

### 6.1 `md_parse_ack(payload, len) -> {cmd, err}`
输入：ACK 帧的 DATA 段（1 字节 err）。`err=0x00` 成功，`0xFF` 失败（兼容固件个别场景 0x01/0x02 —— 任何非 0 均视为失败）。

### 6.2 `md_parse_status(payload, len) -> md_status_t`
输入：0xF0 STATUS_REPORT 的 DATA 段，**56B（常规）/ 72B（扩展）按 len 自动兼容**。

```c
typedef struct {
    int32_t  enc[4];          /* @0   编码器累计脉冲 */
    float    tgt[4];          /* @16  当前目标值 */
    int32_t  rpm[4];          /* @32  滤波后实时转速 */
    int32_t  rpm_raw[4];      /* @48  滤波前原始 RPM（72B 模式有效；56B 模式为 0） */
    uint32_t sbus_frame_cnt;  /* 56B:@48 / 72B:@64 */
    uint32_t sbus_ok_cnt;     /* 56B:@52 / 72B:@68 */
    uint8_t  extended;        /* 1=72B 扩展模式 */
} md_status_t;
```

### 6.3 `md_parse_detect(payload, len) -> {proto, inv, baud}`
0xF1 DETECT_REPORT：`[proto:1B][inv:1B][baud:4B LE]`。proto：0=失败 1=SBUS 2=UART 3=ELRS。

### 6.4 `md_parse_sbus(payload, len) -> ch[16]`
0xF2 SBUS_DATA：`[ch0~15:16×uint16 LE]`。

### 6.5 config_t：`md_parse_config(raw231) -> md_config_t` / `md_pack_config(cfg) -> raw231`
按协议规范 §5 偏移表全字段解析/打包（含位域），往返无损。**md_config_t 字段定义（C）：**

```c
typedef struct {
    /* 通讯 */
    uint32_t baud_rate;
    uint16_t cmd_timeout_ms;
    uint8_t  protocol;          /* comm_flags bit0-3: 1=SBUS 2=UART 3=ELRS */
    uint8_t  sbus_inv;          /* bit4 */
    uint8_t  ctrl_priority;     /* bit5: 0=USART2 优先 1=USB 优先 */
    /* 电机×4 */
    uint8_t  control_mode[4];   /* control_mode 每电机2bit: 0开环 1速度 2位置 */
    uint8_t  motor_invert[4];   /* motor_invert 每电机2bit: bit0引脚反转 bit1编码器极性 */
    uint16_t encoder_cpr[4];
    uint16_t speed_period_ms[4];
    uint8_t  speed_pid_type[4]; /* 每电机4bit: 0位置式 1增量式 */
    uint16_t speed_olim[4];
    float    speed_kp[4], speed_ki[4], speed_kd[4], speed_ilim[4];
    uint16_t pos_period_ms[4];
    uint8_t  pos_pid_type[4];
    float    pos_kp[4], pos_ki[4], pos_kd[4], pos_ilim[4];
    float    pos_olim[4];
    uint16_t pos_angle_cpr[4];
    uint8_t  speed_filter_type[4];   /* 每电机4bit: 0无 1滑动平均 2低通 3中值 */
    uint8_t  speed_filter_window[4];
    uint8_t  sbus_channel[4];   /* sbus_channel_pack 每电机4bit: 0~15=CH1~16（存0表示CH1，解析+1） */
    uint8_t  rc_dir_ch[4];      /* rc_dir_ch 每电机4bit（同上） */
    uint8_t  rc_map_mode[4];    /* rc_map_mode bit0-3 每电机1bit: 0中心零点 1min零点 */
    uint8_t  rc_dir_en[4];      /* rc_map_mode bit4-7 每电机1bit */
    uint16_t sbus_param[4];
    uint16_t sbus_range_min, sbus_range_max;
} md_config_t;
```

> **位域解析约定**：`control_mode`/`motor_invert` 用 `(raw[18] >> (ch*2)) & 0x03` 等位运算；`sbus_channel` 存储值 0~15 表示 CH1~16，结构体里用 1~16（解析时 +1，打包时 -1）；`md_pack_config` 时受保护区（offset 0~10）置 0（固件写入时自动还原受保护字段）。
>
> 51 等小内存平台：config 全字段函数用宏 `MD_ENABLE_CONFIG`（默认 1）开关，置 0 可编译掉以省 RAM（README 说明）。

---

## 7. 各平台实现要求

| 平台 | 文件 | 备注 |
|------|------|------|
| python | `mdc_lib.py`（函数式 + `MDC` 类）+ `test_mdc_lib.py`（自测） | 纯标准库，零依赖 |
| cpp | `mdc_lib.hpp`（header-only，C++17）+ `test_mdc_lib.cpp` | 零依赖 |
| stm32/hal | `mdc_lib.h` + `mdc_lib.c` + `README.md` | 纯 C，不 include HAL 头 |
| esp32/arduino、rp2040/arduino、avr/arduino_uno、esp8266/arduino | `mdc_lib.h` + `mdc_lib.cpp` + `README.md` | 纯 C++，不 include Arduino 头；README 给 `Serial.write(buf, len)` 用法 |
| esp32/micropython、rp2040/micropython、esp8266/micropython | `mdc_lib.py` + `README.md` | 纯 MicroPython（无 machine 依赖），README 给 UART 用法 |
| esp32/esp-idf | `mdc_lib.h` + `mdc_lib.c` + `CMakeLists.txt`（组件）+ `README.md` | 纯 C 组件 |
| rp2040/c-sdk | `mdc_lib.h` + `mdc_lib.c` + `CMakeLists.txt` + `README.md` | 纯 C 库 |
| 51/keil | `mdc_lib.h` + `mdc_lib.c` + `README.md` | C89 兼容、英文注释（Keil 编码兼容）、大数组用 `xdata`、`MD_PARSER_BUF` 可裁剪 |

**通用要求：**
- 头文件含完整中文注释（51 例外用英文），函数签名与本文档**逐一对应**。
- 嵌入式 C 的 `md_bin_motor_ctrl` 等带多个数值参数的函数，用「参数 + 输出缓冲」形态：
  `uint16_t md_bin_motor_ctrl(int32_t t0, int32_t t1, int32_t t2, int32_t t3, uint8_t* out, uint16_t cap)`
- int32/uint16 多字节一律手动移位拼装（不用 union/结构体对齐）。
- 每个平台 README 至少包含：功能、API 速览、**串口接入示例**（用户实现收发，调用库打包/解析）、集成步骤、常见问题。

---

## 8. 一致性验证要求（每个实现交付前必须自检）

| 用例 | 期望 |
|------|------|
| `crc8([0x01,0x00])` | `0x15` |
| `crc8(b"123456789")` | `0xF4` |
| `build_frame(0x01, b"")` | `AA 01 00 15` |
| `build_frame(0x40, [0x32,0x00])`（SUBSCRIBE 50ms） | `AA 40 02 32 00 <crc>` |
| `motor_ctrl(100,-200,0,300)` DATA | `64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00` |
| `parse_status` 56B / 72B | 字段与 §6.2 偏移一致 |
| `parse_config` ↔ `pack_config` 往返 | 无损（位域、float、数组全部一致） |
| 流式解析器：垃圾字节 + 坏 CRC 帧 + 正常帧 | 只输出正常帧 |

---

> **返回：** [mdc_lib 总览 README](README.md)
