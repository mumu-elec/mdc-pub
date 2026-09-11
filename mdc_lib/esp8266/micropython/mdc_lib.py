# -*- coding: utf-8 -*-
"""
mdc_lib.py — Motor Driver Controller 通用调用库（MicroPython 实现）
===================================================================

四路直流电机驱动器（STM32F401 + TB6612）的通信协议库：
只负责「打包要发送的数据」与「解析收到的数据」，
串口收发由用户自己实现（拿返回的 bytes 自行 uart.write()；
收到的字节喂给解析函数或 MDParser 流式解析器）。

**协议依据：** `协议规范.md`（布局 v2.x，config_t=248B，
27 条文本指令，19 条二进制命令）
**API 依据：** `../../API.md`（统一 API 规范，函数命名 / 参数顺序 / 返回约定
与该文档逐一对应；本文件为 ESP32 / RP2040 / ESP8266 三平台共用的 MicroPython 实现）

**平台无关性（重要）：**
    * 纯 MicroPython / CPython 兼容：**零 import**（无 machine / ustruct / struct /
      math 等任何依赖），只用内置类型（int / bytes / bytearray / list / dict /
      str）与字节运算、四则运算
    * 因此本文件在 ESP32 / RP2040 / ESP8266 / 任意 Python 3 环境均可直接 import；
      在 CPython 上可用 `python -m py_compile` 做语法检查
    * 多字节字段一律小端序（LE），用手动移位拼装；IEEE754 单精度浮点
      打包/解析也是纯整数+浮点运算实现（无 struct 依赖）

**返回约定（API.md §2.3）：**
    * 打包函数返回 `bytes`；文本函数返回 `bytes`（含 '\\n'，UTF-8）
    * 解析函数返回 dict / 轻量 namedtuple（本实现用零依赖的 `_NT` 简单类，
      支持属性访问、下标、迭代与解包）；参数非法抛 `ValueError`

**用法示例（用户实现串口收发，以 ESP32 UART2 为例）：**

    from machine import Pin, UART
    import mdc_lib

    uart = UART(2, baudrate=115200, tx=Pin(17), rx=Pin(16), timeout=50)

    # ① 打包 → 发送
    uart.write(mdc_lib.md_bin_motor_ctrl(100, 0, 0, 0))   # 0x31 四通道批量控制
    uart.write(mdc_lib.md_text_mode(1, "speed"))          # 文本指令：/mode 1 speed\\n

    # ② 接收 → 流式解析
    parser = mdc_lib.MDParser()
    n = uart.any()
    if n:
        for b in uart.read(n):
            r = parser.feed(b)                            # 完整帧返回 (cmd, payload)
            if r and r[0] == 0xF0:                        # 0xF0 STATUS_REPORT
                st = mdc_lib.md_parse_status(r[1])
                print("rpm:", st.rpm, "enc:", st.enc)

**验证向量（API.md §8）：**
    md_crc8([0x01,0x00])          == 0x15
    md_crc8(b"123456789")         == 0xF4
    md_bin_ping()                 == b"\\xAA\\x01\\x00\\x15"
    md_bin_motor_ctrl(100,-200,0,300) 的 DATA 段 ==
        64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00
"""

# ---------------------------------------------------------------------------
# 常量（API.md §2.5，所有平台一致）
# ---------------------------------------------------------------------------
MD_SYNC = 0xAA            # 二进制帧同步字
MD_MAX_DATA = 248         # DATA 段最大长度（= config_t 大小）
MD_CONFIG_SIZE = 248      # config_t 大小
MD_FRAME_MAX = 252        # 最大整帧长度（4 + 248）
MD_CRC8_POLY = 0x07       # CRC8 多项式
MD_CMD_PING = 0x01        # 二进制命令号（全表见 §5）
MD_ERR_OK = 0x00          # ACK 成功
MD_ERR_FAIL = 0xFF        # ACK 失败
MD_PARSER_BUF = 256       # 流式解析器缓冲上限（默认，可裁剪）

# 二进制命令字（协议规范 §3.3，19 条）
MD_CMD_READ_PARAM = 0x10
MD_CMD_WRITE_PARAM = 0x11
MD_CMD_WRITE_FIELD = 0x12
MD_CMD_SAVE_EEPROM = 0x20
MD_CMD_LOAD_EEPROM = 0x21
MD_CMD_FACTORY_RESET = 0x22
MD_CMD_MOTOR_RAW = 0x30
MD_CMD_MOTOR_CTRL = 0x31
MD_CMD_MOTOR_JOG = 0x32   # 单轮点动（仅底盘模式，验向用）
MD_CMD_SUBSCRIBE = 0x40
MD_CMD_UNSUBSCRIBE = 0x41
MD_CMD_DEBUG_SBUS = 0x43
MD_CMD_DEBUG_SPEED = 0x44
MD_CMD_ENTER_BL = 0x52
MD_CMD_REBOOT = 0x53
MD_CMD_STATUS_REPORT = 0xF0
MD_CMD_DETECT_REPORT = 0xF1
MD_CMD_SBUS_DATA = 0xF2

__all__ = [
    "MD_SYNC", "MD_MAX_DATA", "MD_CONFIG_SIZE", "MD_FRAME_MAX",
    "MD_CRC8_POLY", "MD_CMD_PING", "MD_ERR_OK", "MD_ERR_FAIL", "MD_PARSER_BUF",
    "MD_CMD_READ_PARAM", "MD_CMD_WRITE_PARAM", "MD_CMD_WRITE_FIELD",
    "MD_CMD_SAVE_EEPROM", "MD_CMD_LOAD_EEPROM", "MD_CMD_FACTORY_RESET",
    "MD_CMD_MOTOR_RAW", "MD_CMD_MOTOR_CTRL", "MD_CMD_MOTOR_JOG",
    "MD_CMD_SUBSCRIBE",
    "MD_CMD_UNSUBSCRIBE", "MD_CMD_DEBUG_SBUS", "MD_CMD_DEBUG_SPEED",
    "MD_CMD_ENTER_BL", "MD_CMD_REBOOT", "MD_CMD_STATUS_REPORT",
    "MD_CMD_DETECT_REPORT", "MD_CMD_SBUS_DATA",
    "md_crc8", "md_build_frame", "md_parse_frame", "MDParser",
    "md_text_build", "md_text_version", "md_text_help", "md_text_status",
    "md_text_check", "md_text_detect", "md_text_save", "md_text_load",
    "md_text_reset", "md_text_enczero", "md_text_mode",
    "md_bin_ping", "md_bin_read_param", "md_bin_write_param",
    "md_bin_write_field", "md_bin_save", "md_bin_load", "md_bin_factory_reset",
    "md_bin_motor_raw", "md_bin_motor_ctrl", "md_bin_motor_jog",
    "md_bin_subscribe",
    "md_bin_unsubscribe", "md_bin_debug_sbus", "md_bin_debug_speed",
    "md_bin_enter_bl", "md_bin_reboot",
    "md_parse_ack", "md_parse_status", "md_parse_detect", "md_parse_sbus",
    "md_parse_config", "md_pack_config",
    "Frame", "Ack", "md_status_t", "Detect", "Sbus", "MDC",
]


# ---------------------------------------------------------------------------
# 内部工具（零依赖：不用 struct / math）
# ---------------------------------------------------------------------------
def _as_bytes(value, what):
    """把 bytes / bytearray 归一化为 bytes；其余类型抛 ValueError。"""
    if isinstance(value, bytearray):
        return bytes(value)
    if isinstance(value, bytes):
        return value
    raise ValueError("%s 需要 bytes/bytearray，实际 %r" % (what, type(value).__name__))


def _check_int(value, lo, hi, name):
    """校验整数范围（拒绝 bool），返回原值。"""
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError("%s 需要整数，实际 %r" % (name, value))
    if not lo <= value <= hi:
        raise ValueError("%s 越界：%d（允许 %d~%d）" % (name, value, lo, hi))
    return value


def _check_float(value, name):
    """校验有限浮点数（可被 f32 表示），返回 float。"""
    try:
        v = float(value)
    except (TypeError, ValueError):
        raise ValueError("%s 需要数值，实际 %r" % (name, value))
    if v != v or v in (float("inf"), float("-inf")):
        raise ValueError("%s 需要有限数值，实际 %r" % (name, value))
    return v


def _check_list(value, count, name):
    """校验长度为 count 的列表/元组。"""
    if not isinstance(value, (list, tuple)) or len(value) != count:
        raise ValueError("%s 需要 %d 个元素，实际 %r" % (name, count, value))
    return value


# ---- 小端字节拼装 / 拆解（纯移位，多字节字段一律 LE）----
def _u16le(v):
    """uint16 -> 2B 小端 bytes"""
    return bytes((v & 0xFF, (v >> 8) & 0xFF))


def _u32le(v):
    """uint32 -> 4B 小端 bytes"""
    return bytes((v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF))


def _i32le(v):
    """int32（含负数）-> 4B 小端 bytes（补码）"""
    v &= 0xFFFFFFFF
    return bytes((v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF))


def _get_u16(b, off):
    """读 2B 小端 uint16"""
    return b[off] | (b[off + 1] << 8)


def _get_u32(b, off):
    """读 4B 小端 uint32"""
    return (b[off] | (b[off + 1] << 8) | (b[off + 2] << 16) | (b[off + 3] << 24)) & 0xFFFFFFFF


def _get_i32(b, off):
    """读 4B 小端 int32（补码）"""
    u = _get_u32(b, off)
    return u - 0x100000000 if u >= 0x80000000 else u


def _put_u16(buf, off, v):
    """向 bytearray 写入 2B 小端 uint16"""
    buf[off] = v & 0xFF
    buf[off + 1] = (v >> 8) & 0xFF


def _put_u32(buf, off, v):
    """向 bytearray 写入 4B 小端 uint32"""
    buf[off] = v & 0xFF
    buf[off + 1] = (v >> 8) & 0xFF
    buf[off + 2] = (v >> 16) & 0xFF
    buf[off + 3] = (v >> 24) & 0xFF


def _put_f32(buf, off, x):
    """向 bytearray 写入 4B 小端 f32（IEEE754 单精度）"""
    _put_u32(buf, off, _f32_to_u32(x))


# ---- IEEE754 单精度浮点（纯运算，兼容单/双精度环境）----
def _pow2(k):
    """计算 2^k（k 为整数）：循环乘/除 2，避免 pow() 舍入（兼容 f32/f64）。"""
    r = 1.0
    if k >= 0:
        for _ in range(k):
            r *= 2.0
    else:
        for _ in range(-k):
            r *= 0.5
    return r


def _round_tie_even(v):
    """把非负浮点数 v 舍入为最近整数；恰为 .5 时舍入到偶数（IEEE754 默认）。

    与 struct '<f' 的 round-half-even 一致：例如 8388607.5 -> 8388608。
    """
    k = int(v)                              # 截断
    if v == k + 0.5:                        # 恰为半值
        return k if (k % 2 == 0) else k + 1
    return int(v + 0.5)


def _f32_to_u32(x):
    """float -> IEEE754 单精度位模式（舍入到最近，平局取偶，与 struct '<f' 一致）。"""
    x = _check_float(x, "float")            # 拒绝 NaN / Inf
    if x == 0.0:
        return 0x80000000 if str(x).startswith("-") else 0   # 保留负零
    sign = 0x80000000 if x < 0 else 0
    if x < 0:
        x = -x
    e = 0
    while x >= 2.0:                         # 归一化到 [1,2)，同时求二进制指数 e
        x *= 0.5
        e += 1
    while x < 1.0:
        x *= 2.0
        e -= 1
    if e < -126:                            # 次正规数（denormal）
        t = x * _pow2(e + 149)              # t = x * 2^(e+149)，e+149 ∈ [0,22]
        m = _round_tie_even(t)
        if m >= 8388608:                    # 进位到最小正规数 2^-126
            return sign | (1 << 23)
        return sign | m
    frac = _round_tie_even((x - 1.0) * 8388608.0)
    if frac >= 8388608:                     # 尾数进位：指数 +1
        frac = 0
        e += 1
    if e > 127:
        raise ValueError("浮点数超出 f32 可表示范围")
    return sign | ((e + 127) << 23) | frac


def _f32_from_u32(u):
    """IEEE754 单精度位模式 -> float（纯运算；Inf/NaN 原样返回）。"""
    sign = -1.0 if (u & 0x80000000) else 1.0
    exp = (u >> 23) & 0xFF
    frac = u & 0x7FFFFF
    if exp == 0xFF:                         # Inf / NaN
        if frac == 0:
            return float("-inf") if sign < 0 else float("inf")
        return float("nan")
    if exp == 0:                            # 次正规数（含 ±0）
        return sign * frac * _pow2(-149)
    return sign * (1.0 + frac / 8388608.0) * _pow2(exp - 127)


def _pack_2bit(vals, name):
    """每电机 2bit 打包（control_mode / motor_invert 等）：val 0~3。"""
    vals = _check_list(vals, 4, name)
    b = 0
    for ch, v in enumerate(vals):
        b |= (_check_int(v, 0, 3, name) & 0x03) << (ch * 2)
    return b


def _pack_4bit(vals, name):
    """每电机 4bit 打包（pid_type / filter_type 等）：val 0~15。"""
    vals = _check_list(vals, 4, name)
    w = 0
    for ch, v in enumerate(vals):
        w |= (_check_int(v, 0, 15, name) & 0x0F) << (ch * 4)
    return w


def _pack_channel(vals, name):
    """遥控通道 4bit 打包（sbus_channel / rc_dir_ch）：API 值 1~16，存 0~15。"""
    vals = _check_list(vals, 4, name)
    w = 0
    for ch, v in enumerate(vals):
        v = _check_int(v, 1, 16, name)
        w |= ((v - 1) & 0x0F) << (ch * 4)
    return w


def _pack_float16(raw, offset, kp, ki, kd, ilim):
    """四通道 kp/ki/kd/ilim → 16 个 f32（每通道 [kp, ki, kd, ilim] 交错）。"""
    _check_list(kp, 4, "kp")
    _check_list(ki, 4, "ki")
    _check_list(kd, 4, "kd")
    _check_list(ilim, 4, "ilim")
    for ch in range(4):
        _put_f32(raw, offset + 16 * ch + 0, _check_float(kp[ch], "kp"))
        _put_f32(raw, offset + 16 * ch + 4, _check_float(ki[ch], "ki"))
        _put_f32(raw, offset + 16 * ch + 8, _check_float(kd[ch], "kd"))
        _put_f32(raw, offset + 16 * ch + 12, _check_float(ilim[ch], "ilim"))


# ---------------------------------------------------------------------------
# §3 底层：CRC / 组帧 / 帧解析
# ---------------------------------------------------------------------------
def md_crc8(data):
    """CRC8 校验（多项式 0x07，初值 0，按位计算），返回 0~255 的整数。

    与《协议规范.md》§3.1 的 C 参考实现逐位等价（CRC-8/ATM）。
    校验向量：``md_crc8([0x01, 0x00]) == 0x15``；``md_crc8(b"123456789") == 0xF4``。
    参数接受 bytes / bytearray / 0~255 整数列表。
    """
    crc = 0
    for b in data:
        if isinstance(b, bool) or not isinstance(b, int) or not 0 <= b <= 0xFF:
            raise ValueError("md_crc8 数据元素需要 0~255 的整数，实际 %r" % (b,))
        crc ^= b
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ MD_CRC8_POLY) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def md_build_frame(cmd, data=b""):
    """组帧：``[0xAA][CMD][LEN][DATA...][CRC8]``，返回完整帧 bytes。

    CRC 计算范围 = CMD + LEN + DATA（不含 SYNC）。
    验证向量：``md_build_frame(0x01, b"") == b"\\xAA\\x01\\x00\\x15"``。

    :param cmd:  命令字（0~255）
    :param data: DATA 段（bytes/bytearray，长度 ≤ MD_MAX_DATA=248）
    :raises ValueError: cmd 越界或 data 超长
    """
    cmd = _check_int(cmd, 0, 0xFF, "cmd")
    data = _as_bytes(data, "data")
    if len(data) > MD_MAX_DATA:
        raise ValueError("DATA 长度 %d 超过上限 %d" % (len(data), MD_MAX_DATA))
    body = bytes((cmd, len(data))) + data
    return bytes((MD_SYNC,)) + body + bytes((md_crc8(body),))


class _NT:
    """轻量 namedtuple 替代（零依赖，MicroPython 兼容）。

    支持属性访问（``st.enc``）、下标（``st[0]``）、迭代与解包（``a, b = st``）。
    字段顺序由各子类构造时给定（见 Frame / Ack / md_status_t / Detect / Sbus）。
    """

    __slots__ = ("_fields", "_vals")

    def __init__(self, fields, vals):
        self._fields = tuple(fields)
        self._vals = tuple(vals)

    def __getattr__(self, name):
        for i, f in enumerate(self._fields):
            if f == name:
                return self._vals[i]
        raise AttributeError("'%s' object has no field %r"
                             % (self.__class__.__name__, name))

    def __getitem__(self, i):
        return self._vals[i]

    def __len__(self):
        return len(self._vals)

    def __iter__(self):
        return iter(self._vals)

    def __repr__(self):
        return "%s(%s)" % (self.__class__.__name__,
                           ", ".join("%s=%r" % (f, v)
                                     for f, v in zip(self._fields, self._vals)))

    def __eq__(self, other):
        return (isinstance(other, _NT)
                and self._fields == other._fields
                and self._vals == other._vals)


class Frame(_NT):
    """帧级解析结果：cmd=命令字，payload=DATA 段，valid=是否有效。"""

    __slots__ = ()

    def __init__(self, cmd, payload, valid):
        _NT.__init__(self, ("cmd", "payload", "valid"), (cmd, payload, valid))


def md_parse_frame(frame):
    """帧级解析：校验 SYNC 与 CRC，返回 ``Frame(cmd, payload, valid)``。

    * valid=True：cmd = 命令字，payload = DATA 段 bytes（长度 = LEN）
    * valid=False：帧无效（SYNC 不符 / 长度不符 / CRC 失败），cmd/payload 无意义

    与 C 版 `int md_parse_frame(...)` 返回 valid 标志的语义一致：
    无效帧不抛异常（字节流中出现坏帧是正常情况），仅输入类型非法抛 ValueError。
    """
    frame = _as_bytes(frame, "frame")
    if len(frame) < 4 or frame[0] != MD_SYNC:
        return Frame(0, b"", False)
    plen = frame[2]
    if plen > MD_MAX_DATA or len(frame) != 4 + plen:
        return Frame(0, b"", False)
    body = frame[1:3 + plen]                      # CMD + LEN + DATA
    if md_crc8(body) != frame[3 + plen]:
        return Frame(0, b"", False)
    return Frame(frame[1], frame[3:3 + plen], True)


class MDParser:
    """流式解析器：逐字节喂入，自动找 0xAA 同步 + CRC8 校验。

    行为（API.md §3.4）：
        * 滑动窗口找 0xAA；LEN > max_data（默认 248）时丢弃该同步字重扫
        * CRC 失败丢弃该字节继续向后搜索，可容忍串口上的杂散字节
        * 收到完整且 CRC 通过的一帧时返回 ``(cmd, payload)``，否则返回 None
        * 未消费的剩余字节保留在内部缓冲，适配连续上报流（两帧连发等）

    与二进制帧混流的文本回显行会被当作噪声丢弃（0xAA 前的字节全部丢弃）。
    """

    def __init__(self, max_data=MD_MAX_DATA):
        """构造解析器。max_data 可调小（51 等小内存平台可裁剪 MD_PARSER_BUF）。"""
        if not isinstance(max_data, int) or not 0 < max_data <= MD_MAX_DATA:
            raise ValueError("max_data 需要 1~%d 的整数" % MD_MAX_DATA)
        self.max_data = max_data
        self.max_frame = 4 + max_data             # 一帧最多 4 + max_data 字节
        self.buf = bytearray()                    # 内部接收缓冲

    def reset(self):
        """清空内部缓冲，回到初始状态。"""
        self.buf.clear()

    def feed(self, byte):
        """喂入一个字节（0~255 的整数）。

        :return: 收到完整且 CRC 通过的一帧时返回 ``(cmd:int, payload:bytes)``，
                 否则返回 None。
        :raises ValueError: 参数不是 0~255 的整数
        """
        if isinstance(byte, bool) or not isinstance(byte, int) or not 0 <= byte <= 0xFF:
            raise ValueError("feed 需要 0~255 的整数，实际 %r" % (byte,))
        self.buf.append(byte)
        while True:
            idx = self.buf.find(b"\xAA")          # 找同步字
            if idx < 0:                           # 无同步字：整段都是噪声
                self.buf.clear()
                return None
            if idx > 0:                           # 丢弃同步字前的杂散字节
                del self.buf[:idx]
            if len(self.buf) < 3:                 # 至少需要 CMD + LEN 两个字节
                return None
            plen = self.buf[2]                    # DATA 段长度
            if plen > self.max_data:              # LEN 非法：丢弃该同步字重扫
                del self.buf[0]
                continue
            need = 3 + plen + 1                   # CMD+LEN+DATA+CRC
            if len(self.buf) < need:              # 整帧未收完：继续等待
                if len(self.buf) >= self.max_frame:   # 缓冲已满仍凑不齐：丢弃
                    del self.buf[0]
                    continue
                return None
            body = bytes(self.buf[1:3 + plen])    # CRC 计算范围
            if md_crc8(body) == self.buf[3 + plen]:
                cmd = self.buf[1]
                payload = bytes(self.buf[3:3 + plen])
                del self.buf[:need]               # 消费整帧，剩余字节保留
                return (cmd, payload)
            del self.buf[0]                       # CRC 失败：丢弃同步字继续扫描


# ---------------------------------------------------------------------------
# §4 文本指令层（返回要发送的文本行，含 '\n'）
# ---------------------------------------------------------------------------
def md_text_build(cmd, args=None):
    """构造文本指令行：``cmd`` 缺 '/' 前缀时自动补上。

    * ``md_text_build("/mode", "1 speed")`` -> ``b"/mode 1 speed\\n"``
    * ``md_text_build("/mode", None)``       -> ``b"/mode\\n"``（省略参数 = 读取模式）
    * 内部拼接不信任输入长度，args 会被 str() 转换。

    :raises ValueError: cmd 不是非空字符串
    """
    if not isinstance(cmd, str) or not cmd:
        raise ValueError("cmd 需要非空字符串，实际 %r" % (cmd,))
    if not cmd.startswith("/"):
        cmd = "/" + cmd
    if args is None:
        return (cmd + "\n").encode("utf-8")
    return ("%s %s\n" % (cmd, args)).encode("utf-8")


def md_text_version():
    """``/version\\n``：显示硬件/软件版本。"""
    return md_text_build("/version")


def md_text_help():
    """``/help\\n``：打印所有可用命令。"""
    return md_text_build("/help")


def md_text_status():
    """``/status\\n``：打印全部配置参数。"""
    return md_text_build("/status")


def md_text_check():
    """``/check\\n``：显示实时状态（编码器/RPM/输出/运行时间）。"""
    return md_text_build("/check")


def md_text_detect():
    """``/detect\\n``：重新检测 USART2 工作模式/波特率。"""
    return md_text_build("/detect")


def md_text_save():
    """``/save\\n``：当前 RAM 配置写入 EEPROM。"""
    return md_text_build("/save")


def md_text_load():
    """``/load\\n``：从 EEPROM 重新加载配置到 RAM。"""
    return md_text_build("/load")


def md_text_reset():
    """``/reset\\n``：恢复出厂默认值。"""
    return md_text_build("/reset")


def md_text_enczero(ch):
    """``/enczero <ch>\\n``：清零指定通道（1~4）编码器累计计数。"""
    ch = _check_int(ch, 1, 4, "ch")
    return md_text_build("/enczero", str(ch))


def md_text_mode(ch, mode=None):
    """``/mode <ch> [mode]\\n``：设置通道控制模式。

    * ``md_text_mode(1)``          -> ``b"/mode 1\\n"``（省略 = 读取模式）
    * ``md_text_mode(1, "speed")`` -> ``b"/mode 1 speed\\n"``
    * mode 取值：open / speed / pos。
    """
    ch = _check_int(ch, 1, 4, "ch")
    return md_text_build("/mode", str(ch) if mode is None else "%d %s" % (ch, mode))


# ---------------------------------------------------------------------------
# §5 二进制命令层（16 个打包函数，返回整帧字节）
# ---------------------------------------------------------------------------
def md_bin_ping():
    """0x01 PING：连通性测试。验证向量：``== b"\\xAA\\x01\\x00\\x15"``。"""
    return md_build_frame(MD_CMD_PING)


def md_bin_read_param():
    """0x10 READ_PARAM：读取全部配置（应答为 config_t 248B）。"""
    return md_build_frame(MD_CMD_READ_PARAM)


def md_bin_write_param(cfg):
    """0x11 WRITE_PARAM：写入全部配置（仅 RAM，受保护字段自动还原）。

    :param cfg: 248B bytes（config_t），或字段 dict（自动经 md_pack_config 打包）
    """
    if isinstance(cfg, dict):
        data = md_pack_config(cfg)
    else:
        data = _as_bytes(cfg, "cfg")
        if len(data) != MD_CONFIG_SIZE:
            raise ValueError("WRITE_PARAM 的 config_t 应为 %dB，实际 %dB"
                             % (MD_CONFIG_SIZE, len(data)))
    return md_build_frame(MD_CMD_WRITE_PARAM, data)


def md_bin_write_field(field_id, value, value_len=None):
    """0x12 WRITE_FIELD：按偏移量写入单个字段。

    DATA 布局：``[field_id:2B LE][value:nB]``。
    * value 为 bytes/bytearray 时 value_len 缺省取 len(value)
    * value 为 int 时必须给 value_len（1/2/4），按小端打包

    注意：固件拒绝 offset < 11（受保护区 11B 头部，错误码 0x02），库只负责打包，不代做该检查。
    """
    field_id = _check_int(field_id, 0, 0xFFFF, "field_id")
    if isinstance(value, (bytes, bytearray)):
        raw = _as_bytes(value, "value")
        if value_len is not None and value_len != len(raw):
            raise ValueError("value_len=%d 与 value 长度 %d 不符"
                             % (value_len, len(raw)))
    elif isinstance(value, int) and not isinstance(value, bool):
        if value_len not in (1, 2, 4):
            raise ValueError("int 形式的 value 需要 value_len=1/2/4")
        _check_int(value, 0, (1 << (8 * value_len)) - 1, "value")
        if value_len == 1:
            raw = bytes((value,))
        elif value_len == 2:
            raw = _u16le(value)
        else:
            raw = _u32le(value)
    else:
        raise ValueError("value 需要 bytes/bytearray（或 int + value_len）")
    data = _u16le(field_id) + raw
    return md_build_frame(MD_CMD_WRITE_FIELD, data)


def md_bin_save():
    """0x20 SAVE_EEPROM：RAM 配置写入 EEPROM（约 190ms）。"""
    return md_build_frame(MD_CMD_SAVE_EEPROM)


def md_bin_load():
    """0x21 LOAD_EEPROM：从 EEPROM 加载到 RAM。"""
    return md_build_frame(MD_CMD_LOAD_EEPROM)


def md_bin_factory_reset():
    """0x22 FACTORY_RESET：恢复出厂默认（RAM+EEPROM）。"""
    return md_build_frame(MD_CMD_FACTORY_RESET)


def md_bin_motor_raw(ch, dir_, pwm):
    """0x30 MOTOR_RAW：单通道 PWM 直驱（旁路 PID，受优先级仲裁）。

    DATA：``[ch:1B][dir:1B][pwm:2B LE]``；ch=0~3，dir=0 正转/1 反转，pwm=0~1000。
    """
    ch = _check_int(ch, 0, 3, "ch")
    dir_ = _check_int(dir_, 0, 1, "dir")
    pwm = _check_int(pwm, 0, 1000, "pwm")
    return md_build_frame(MD_CMD_MOTOR_RAW, bytes((ch, dir_)) + _u16le(pwm))


def md_bin_motor_ctrl(t0, t1, t2, t3):
    """0x31 MOTOR_CTRL：四通道批量控制（核心控制帧，int32 LE，受优先级仲裁）。

    目标值含义随各通道控制模式：开环 = PWM（±1000）、速度 = RPM、
    位置 = 0.1°（±3600 = ±360.0°）。支持负数。
    验证向量：``md_bin_motor_ctrl(100, -200, 0, 300)`` 的 DATA 段 ==
    ``64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00``。
    """
    vals = [_check_int(v, -0x80000000, 0x7FFFFFFF, "t%d" % i)
            for i, v in enumerate((t0, t1, t2, t3))]
    return md_build_frame(MD_CMD_MOTOR_CTRL,
                          _i32le(vals[0]) + _i32le(vals[1]) +
                          _i32le(vals[2]) + _i32le(vals[3]))


def md_bin_motor_jog(ch, rpm):
    """0x32 MOTOR_JOG：单轮点动（仅底盘模式生效，用于验向）。

    DATA：``[ch:1B][rpm:2B LE 有符号]``；ch=0~3 物理通道，rpm 为有符号 int16。
    * 仅底盘模式（chassis_type≠0）生效：覆盖该物理通道轮目标低速转动
    * rpm=0 清除覆盖；400ms 无刷新自动解除
    * 非底盘模式下本帧被固件忽略
    验证向量：``md_bin_motor_jog(1, -300)`` 的 DATA 段 == ``01 D4 FE``。
    """
    ch = _check_int(ch, 0, 3, "ch")
    rpm = _check_int(rpm, -0x8000, 0x7FFF, "rpm")
    return md_build_frame(MD_CMD_MOTOR_JOG, bytes((ch,)) + _u16le(rpm & 0xFFFF))


def md_bin_subscribe(interval_ms):
    """0x40 SUBSCRIBE：开启状态周期上报。

    DATA：``[interval_ms:2B LE]``。固件钳位 ≥20ms（库不代做该限制，
    与 Python 参考实现一致，由固件兜底）。
    """
    interval_ms = _check_int(interval_ms, 0, 0xFFFF, "interval_ms")
    return md_build_frame(MD_CMD_SUBSCRIBE, _u16le(interval_ms))


def md_bin_unsubscribe():
    """0x41 UNSUBSCRIBE：关闭状态上报。"""
    return md_build_frame(MD_CMD_UNSUBSCRIBE)


def md_bin_debug_sbus(enable):
    """0x43 DEBUG_SBUS：SBUS 16 通道实时上报开关（enable 为真值/0/1）。"""
    return md_build_frame(MD_CMD_DEBUG_SBUS, b"\x01" if enable else b"\x00")


def md_bin_debug_speed(enable):
    """0x44 DEBUG_SPEED：速度原始值上报开关（开启后 STATUS_REPORT 帧 72B）。"""
    return md_build_frame(MD_CMD_DEBUG_SPEED, b"\x01" if enable else b"\x00")


def md_bin_enter_bl():
    """0x52 ENTER_BL：软复位进入 Bootloader（写 RTC magic + SystemReset）。"""
    return md_build_frame(MD_CMD_ENTER_BL)


def md_bin_reboot():
    """0x53 REBOOT：系统重启（ACK 后延迟 100ms）。"""
    return md_build_frame(MD_CMD_REBOOT)


# ---------------------------------------------------------------------------
# §6 解析层
# ---------------------------------------------------------------------------
class Ack(_NT):
    """ACK 解析结果：cmd=命令字（DATA 段输入时为 None），err=错误码（0 成功，非 0 失败）。"""

    __slots__ = ()

    def __init__(self, cmd, err):
        _NT.__init__(self, ("cmd", "err"), (cmd, err))


class md_status_t(_NT):
    """STATUS_REPORT 解析结果（§6.2）：enc/tgt/rpm/rpm_raw 各 4 元素；
    extended=1 表示 72B 扩展模式。字段顺序：
    enc, tgt, rpm, rpm_raw, sbus_frame_cnt, sbus_ok_cnt, extended。"""

    __slots__ = ()

    def __init__(self, enc, tgt, rpm, rpm_raw, sbus_frame_cnt, sbus_ok_cnt, extended):
        _NT.__init__(self, ("enc", "tgt", "rpm", "rpm_raw",
                            "sbus_frame_cnt", "sbus_ok_cnt", "extended"),
                     (enc, tgt, rpm, rpm_raw, sbus_frame_cnt, sbus_ok_cnt, extended))


class Detect(_NT):
    """DETECT_REPORT 解析结果：proto=0 失败 1=SBUS 2=UART 3=ELRS。"""

    __slots__ = ()

    def __init__(self, proto, inv, baud):
        _NT.__init__(self, ("proto", "inv", "baud"), (proto, inv, baud))


class Sbus(_NT):
    """SBUS_DATA 解析结果：ch = 16 个通道值（uint16 LE）。"""

    __slots__ = ()

    def __init__(self, ch):
        _NT.__init__(self, ("ch",), (ch,))


def md_parse_ack(payload):
    """解析 ACK 帧的 DATA 段（1 字节 err），返回 ``Ack(cmd, err)``。

    * err=0x00 成功；任何非 0 均视为失败（兼容固件个别场景 0x01/0x02）
    * 输入为 1 字节 DATA 段时 cmd=None（命令字由帧头提供，调用方已知）；
      也接受完整 ACK 帧（长度 ≥4：``[SYNC][CMD][0x01][err][CRC]``），自动取 cmd。

    :raises ValueError: 输入长度既不是 1 也不是 ≥4
    """
    payload = _as_bytes(payload, "payload")
    if len(payload) == 1:
        return Ack(None, payload[0])
    if len(payload) >= 4:
        return Ack(payload[1], payload[3])
    raise ValueError("ACK 载荷需要 1 字节 err（或完整 ACK 帧），实际 %d 字节"
                     % len(payload))


def md_parse_status(payload):
    """解析 0xF0 STATUS_REPORT 的 DATA 段，56B（常规）/ 72B（扩展）按 len 自动兼容。

    * 56B：``[enc:16B][tgt:16B][rpm:16B][sbus_frame_cnt:4B][sbus_ok_cnt:4B]``
    * 72B：常规基础上追加 ``[rpm_raw:16B]``（需先 0x44 DEBUG_SPEED 开启）

    返回 md_status_t(enc, tgt, rpm, rpm_raw, sbus_frame_cnt, sbus_ok_cnt, extended)：
    * enc / rpm / rpm_raw：int32 各 4 元素；tgt：float 4 元素
    * rpm_raw 在 56B 模式下恒为 (0, 0, 0, 0)；extended=1 表示 72B 扩展模式
    """
    payload = _as_bytes(payload, "payload")
    n = len(payload)
    if n == 56:
        enc = [_get_i32(payload, 0 + 4 * i) for i in range(4)]
        tgt = [_f32_from_u32(_get_u32(payload, 16 + 4 * i)) for i in range(4)]
        rpm = [_get_i32(payload, 32 + 4 * i) for i in range(4)]
        rpm_raw = [0, 0, 0, 0]
        frame_cnt = _get_u32(payload, 48)
        ok_cnt = _get_u32(payload, 52)
        extended = 0
    elif n == 72:
        enc = [_get_i32(payload, 0 + 4 * i) for i in range(4)]
        tgt = [_f32_from_u32(_get_u32(payload, 16 + 4 * i)) for i in range(4)]
        rpm = [_get_i32(payload, 32 + 4 * i) for i in range(4)]
        rpm_raw = [_get_i32(payload, 48 + 4 * i) for i in range(4)]
        frame_cnt = _get_u32(payload, 64)
        ok_cnt = _get_u32(payload, 68)
        extended = 1
    else:
        raise ValueError("未知 STATUS_REPORT 长度：%d（应为 56 或 72）" % n)
    return md_status_t(enc, tgt, rpm, rpm_raw, frame_cnt, ok_cnt, extended)


def md_parse_detect(payload):
    """解析 0xF1 DETECT_REPORT：``[proto:1B][inv:1B][baud:4B LE]``。

    proto：0=识别失败（inv/baud 无效），1=SBUS，2=UART，3=ELRS。
    """
    payload = _as_bytes(payload, "payload")
    if len(payload) != 6:
        raise ValueError("DETECT_REPORT 载荷应为 6B，实际 %d 字节" % len(payload))
    proto, inv = payload[0], payload[1]
    baud = _get_u32(payload, 2)
    return Detect(proto, inv, baud)


def md_parse_sbus(payload):
    """解析 0xF2 SBUS_DATA：``[ch0~15:16×uint16 LE]``，返回 ``Sbus(ch)``。"""
    payload = _as_bytes(payload, "payload")
    if len(payload) != 32:
        raise ValueError("SBUS_DATA 载荷应为 32B，实际 %d 字节" % len(payload))
    return Sbus([_get_u16(payload, 2 * i) for i in range(16)])


# ---------------------------------------------------------------------------
# §6.5 config_t：解析 / 打包（协议规范 §5 偏移表，布局 v2.x，248B）
# ---------------------------------------------------------------------------
# md_pack_config 缺省字段的中性值：数值 0；遥控通道映射默认 CH1（存 0）；
# 电机映射默认 ABCD 顺序（对应固件默认 0xE4）
_CONFIG_DEFAULTS = {
    "baud_rate": 0, "cmd_timeout_ms": 0,
    "protocol": 0, "sbus_inv": 0, "ctrl_priority": 0,
    "control_mode": [0, 0, 0, 0], "motor_invert": [0, 0, 0, 0],
    "encoder_cpr": [0, 0, 0, 0], "speed_period_ms": [0, 0, 0, 0],
    "speed_pid_type": [0, 0, 0, 0], "speed_olim": [0, 0, 0, 0],
    "speed_kp": [0.0, 0.0, 0.0, 0.0], "speed_ki": [0.0, 0.0, 0.0, 0.0],
    "speed_kd": [0.0, 0.0, 0.0, 0.0], "speed_ilim": [0.0, 0.0, 0.0, 0.0],
    "pos_period_ms": [0, 0, 0, 0], "pos_pid_type": [0, 0, 0, 0],
    "pos_kp": [0.0, 0.0, 0.0, 0.0], "pos_ki": [0.0, 0.0, 0.0, 0.0],
    "pos_kd": [0.0, 0.0, 0.0, 0.0], "pos_ilim": [0.0, 0.0, 0.0, 0.0],
    "pos_olim": [0.0, 0.0, 0.0, 0.0], "pos_angle_cpr": [0, 0, 0, 0],
    "speed_filter_type": [0, 0, 0, 0], "speed_filter_window": [0, 0, 0, 0],
    "sbus_channel": [1, 1, 1, 1], "rc_dir_ch": [1, 1, 1, 1],
    "rc_map_mode": [0, 0, 0, 0], "rc_dir_en": [0, 0, 0, 0],
    "sbus_param": [0, 0, 0, 0], "sbus_range_min": 0, "sbus_range_max": 0,
    "chassis_type": 0, "wheel_diameter": 0,
    "chassis_geo": [0, 0, 0], "chassis_max": [0, 0, 0],
    "chassis_accel": [0, 0, 0], "stall_time_ms": 0,
    "motor_map": [0, 1, 2, 3], "rc_dir_default": [0, 0, 0, 0],
}


def md_parse_config(raw):
    """解析 248B config_t → dict（键名与 API.md §6.5 字段一一对应，与 Python 版一致）。

    位域按通道解码（API.md §6.5 位域解析约定）：
        * control_mode / motor_invert / motor_map：每电机 2bit（``(raw[18] >> (ch*2)) & 0x03``）
        * speed_pid_type / pos_pid_type / speed_filter_type：每电机 4bit
        * sbus_channel / rc_dir_ch：每电机 4bit，存储值 0~15 表示 CH1~16，解析 +1
        * rc_map_mode（bit0-3）/ rc_dir_en（bit4-7）：每电机 1bit
        * rc_dir_default：bit0-3 每通道 1bit（ch4 底盘保留）
        * protocol（bit0-3）/ sbus_inv（bit4）/ ctrl_priority（bit5）：来自 comm_flags

    :raises ValueError: 长度不是 248B
    """
    raw = _as_bytes(raw, "raw")
    if len(raw) != MD_CONFIG_SIZE:
        raise ValueError("config_t 应为 %dB，实际 %dB" % (MD_CONFIG_SIZE, len(raw)))

    out = {}
    # 通讯（offset 11/15/17）
    out["baud_rate"] = _get_u32(raw, 11)
    out["cmd_timeout_ms"] = _get_u16(raw, 15)
    flags = raw[17]                               # comm_flags
    out["protocol"] = flags & 0x0F
    out["sbus_inv"] = (flags >> 4) & 0x01
    out["ctrl_priority"] = (flags >> 5) & 0x01
    # 电机×4（offset 18/19）
    cm = raw[18]                                  # control_mode
    out["control_mode"] = [(cm >> (ch * 2)) & 0x03 for ch in range(4)]
    mi = raw[19]                                  # motor_invert
    out["motor_invert"] = [(mi >> (ch * 2)) & 0x03 for ch in range(4)]
    # 速度环（offset 20~105；v2.x period 压缩为 u8）
    out["encoder_cpr"] = [_get_u16(raw, 20 + 2 * ch) for ch in range(4)]
    out["speed_period_ms"] = [raw[28 + ch] for ch in range(4)]
    spt = _get_u16(raw, 32)                       # speed_pid_type
    out["speed_pid_type"] = [(spt >> (ch * 4)) & 0x0F for ch in range(4)]
    out["speed_olim"] = [_get_u16(raw, 34 + 2 * ch) for ch in range(4)]
    out["speed_kp"] = [_f32_from_u32(_get_u32(raw, 42 + 16 * ch)) for ch in range(4)]
    out["speed_ki"] = [_f32_from_u32(_get_u32(raw, 46 + 16 * ch)) for ch in range(4)]
    out["speed_kd"] = [_f32_from_u32(_get_u32(raw, 50 + 16 * ch)) for ch in range(4)]
    out["speed_ilim"] = [_f32_from_u32(_get_u32(raw, 54 + 16 * ch)) for ch in range(4)]
    # 位置环（offset 106~199；v2.x period 压缩为 u8）
    out["pos_period_ms"] = [raw[106 + ch] for ch in range(4)]
    ppt = _get_u16(raw, 110)                      # pos_pid_type
    out["pos_pid_type"] = [(ppt >> (ch * 4)) & 0x0F for ch in range(4)]
    out["pos_kp"] = [_f32_from_u32(_get_u32(raw, 112 + 16 * ch)) for ch in range(4)]
    out["pos_ki"] = [_f32_from_u32(_get_u32(raw, 116 + 16 * ch)) for ch in range(4)]
    out["pos_kd"] = [_f32_from_u32(_get_u32(raw, 120 + 16 * ch)) for ch in range(4)]
    out["pos_ilim"] = [_f32_from_u32(_get_u32(raw, 124 + 16 * ch)) for ch in range(4)]
    out["pos_olim"] = [_f32_from_u32(_get_u32(raw, 176 + 4 * ch)) for ch in range(4)]
    out["pos_angle_cpr"] = [_get_u16(raw, 192 + 2 * ch) for ch in range(4)]
    # 滤波 / 遥控（offset 200~222）
    sft = _get_u16(raw, 200)                      # speed_filter_type
    out["speed_filter_type"] = [(sft >> (ch * 4)) & 0x0F for ch in range(4)]
    out["speed_filter_window"] = [raw[202 + ch] for ch in range(4)]
    scp = _get_u16(raw, 206)                      # sbus_channel_pack
    out["sbus_channel"] = [((scp >> (ch * 4)) & 0x0F) + 1 for ch in range(4)]
    rdc = _get_u16(raw, 208)                      # rc_dir_ch
    out["rc_dir_ch"] = [((rdc >> (ch * 4)) & 0x0F) + 1 for ch in range(4)]
    rmm = raw[210]                                # rc_map_mode
    out["rc_map_mode"] = [(rmm >> ch) & 0x01 for ch in range(4)]
    out["rc_dir_en"] = [(rmm >> (4 + ch)) & 0x01 for ch in range(4)]
    out["sbus_param"] = [_get_u16(raw, 211 + 2 * ch) for ch in range(4)]
    out["sbus_range_min"] = _get_u16(raw, 219)
    out["sbus_range_max"] = _get_u16(raw, 221)
    # 底盘（offset 223~247，v2.0 新增）
    out["chassis_type"] = raw[223]
    out["wheel_diameter"] = _get_u16(raw, 224)
    out["chassis_geo"] = [_get_u16(raw, 226 + 2 * ch) for ch in range(3)]
    out["chassis_max"] = [_get_u16(raw, 232 + 2 * ch) for ch in range(3)]
    out["chassis_accel"] = [_get_u16(raw, 238 + 2 * ch) for ch in range(3)]
    out["stall_time_ms"] = _get_u16(raw, 244)
    mm = raw[246]                                 # motor_map
    out["motor_map"] = [(mm >> (ch * 2)) & 0x03 for ch in range(4)]
    rd = raw[247]                                 # rc_dir_default
    out["rc_dir_default"] = [(rd >> ch) & 0x01 for ch in range(4)]
    return out


def md_pack_config(cfg):
    """把配置 dict 打包为 248B config_t bytes（协议规范 §5 偏移表）。

    * 键名与 md_parse_config 一致；缺省字段取中性值（数值 0，通道映射默认 CH1，
      电机映射默认 ABCD 顺序），因此可用 ``md_parse_config`` 的结果直接回写（往返无损）
    * 受保护区（offset 0~10：magic/hw_ver/sw_ver/sw_ver_hw/crc）恒为 0，
      固件写入时自动还原受保护字段
    * 位域打包与 API.md §6.5 约定一致：sbus_channel / rc_dir_ch 的 API 值
      1~16 存为 0~15

    :raises ValueError: 字段值越界 / 类型非法；未知键抛 KeyError
    """
    if not isinstance(cfg, dict):
        raise ValueError("cfg 需要 dict，实际 %r" % type(cfg).__name__)
    for k in cfg:
        if k not in _CONFIG_DEFAULTS:
            raise KeyError("未知配置字段: %s" % k)
    d = dict(_CONFIG_DEFAULTS)
    d.update(cfg)

    raw = bytearray(MD_CONFIG_SIZE)               # 受保护区 offset 0~10 保持 0
    # 通讯
    _put_u32(raw, 11, _check_int(d["baud_rate"], 0, 0xFFFFFFFF, "baud_rate"))
    _put_u16(raw, 15, _check_int(d["cmd_timeout_ms"], 0, 0xFFFF, "cmd_timeout_ms"))
    flags = ((_check_int(d["protocol"], 0, 15, "protocol") & 0x0F)
             | ((_check_int(d["sbus_inv"], 0, 1, "sbus_inv") & 0x01) << 4)
             | ((_check_int(d["ctrl_priority"], 0, 1, "ctrl_priority") & 0x01) << 5))
    raw[17] = flags
    # 电机位域
    raw[18] = _pack_2bit(d["control_mode"], "control_mode")
    raw[19] = _pack_2bit(d["motor_invert"], "motor_invert")
    # 速度环（v2.x period 为 u8，1~255）
    for i, v in enumerate(_check_list(d["encoder_cpr"], 4, "encoder_cpr")):
        _put_u16(raw, 20 + 2 * i, _check_int(v, 0, 0xFFFF, "encoder_cpr"))
    for i, v in enumerate(_check_list(d["speed_period_ms"], 4, "speed_period_ms")):
        raw[28 + i] = _check_int(v, 0, 0xFF, "speed_period_ms")
    _put_u16(raw, 32, _pack_4bit(d["speed_pid_type"], "speed_pid_type"))
    for i, v in enumerate(_check_list(d["speed_olim"], 4, "speed_olim")):
        _put_u16(raw, 34 + 2 * i, _check_int(v, 0, 0xFFFF, "speed_olim"))
    _pack_float16(raw, 42, d["speed_kp"], d["speed_ki"], d["speed_kd"], d["speed_ilim"])
    # 位置环（v2.x period 为 u8，1~255）
    for i, v in enumerate(_check_list(d["pos_period_ms"], 4, "pos_period_ms")):
        raw[106 + i] = _check_int(v, 0, 0xFF, "pos_period_ms")
    _put_u16(raw, 110, _pack_4bit(d["pos_pid_type"], "pos_pid_type"))
    _pack_float16(raw, 112, d["pos_kp"], d["pos_ki"], d["pos_kd"], d["pos_ilim"])
    for i, v in enumerate(_check_list(d["pos_olim"], 4, "pos_olim")):
        _put_f32(raw, 176 + 4 * i, _check_float(v, "pos_olim"))
    for i, v in enumerate(_check_list(d["pos_angle_cpr"], 4, "pos_angle_cpr")):
        _put_u16(raw, 192 + 2 * i, _check_int(v, 0, 0xFFFF, "pos_angle_cpr"))
    # 滤波 / 遥控
    _put_u16(raw, 200, _pack_4bit(d["speed_filter_type"], "speed_filter_type"))
    for i, v in enumerate(_check_list(d["speed_filter_window"], 4, "speed_filter_window")):
        raw[202 + i] = _check_int(v, 0, 0xFF, "speed_filter_window")
    _put_u16(raw, 206, _pack_channel(d["sbus_channel"], "sbus_channel"))
    _put_u16(raw, 208, _pack_channel(d["rc_dir_ch"], "rc_dir_ch"))
    rmm = 0
    for ch, v in enumerate(_check_list(d["rc_map_mode"], 4, "rc_map_mode")):
        rmm |= (_check_int(v, 0, 1, "rc_map_mode") & 0x01) << ch
    for ch, v in enumerate(_check_list(d["rc_dir_en"], 4, "rc_dir_en")):
        rmm |= (_check_int(v, 0, 1, "rc_dir_en") & 0x01) << (4 + ch)
    raw[210] = rmm
    for i, v in enumerate(_check_list(d["sbus_param"], 4, "sbus_param")):
        _put_u16(raw, 211 + 2 * i, _check_int(v, 0, 0xFFFF, "sbus_param"))
    _put_u16(raw, 219, _check_int(d["sbus_range_min"], 0, 0xFFFF, "sbus_range_min"))
    _put_u16(raw, 221, _check_int(d["sbus_range_max"], 0, 0xFFFF, "sbus_range_max"))
    # 底盘（v2.0 新增；chassis_type 枚举 0~6）
    raw[223] = _check_int(d["chassis_type"], 0, 0xFF, "chassis_type")
    _put_u16(raw, 224, _check_int(d["wheel_diameter"], 0, 0xFFFF, "wheel_diameter"))
    for i, v in enumerate(_check_list(d["chassis_geo"], 3, "chassis_geo")):
        _put_u16(raw, 226 + 2 * i, _check_int(v, 0, 0xFFFF, "chassis_geo"))
    for i, v in enumerate(_check_list(d["chassis_max"], 3, "chassis_max")):
        _put_u16(raw, 232 + 2 * i, _check_int(v, 0, 0xFFFF, "chassis_max"))
    for i, v in enumerate(_check_list(d["chassis_accel"], 3, "chassis_accel")):
        _put_u16(raw, 238 + 2 * i, _check_int(v, 0, 0xFFFF, "chassis_accel"))
    _put_u16(raw, 244, _check_int(d["stall_time_ms"], 0, 0xFFFF, "stall_time_ms"))
    raw[246] = _pack_2bit(d["motor_map"], "motor_map")
    rd = 0
    for ch, v in enumerate(_check_list(d["rc_dir_default"], 4, "rc_dir_default")):
        rd |= (_check_int(v, 0, 1, "rc_dir_default") & 0x01) << ch
    raw[247] = rd
    return bytes(raw)


# ---------------------------------------------------------------------------
# §2.3 MDC 便捷类（可选，函数式 API 为准）
# ---------------------------------------------------------------------------
class MDC:
    """Motor Driver Controller 便捷类：封装 mdc_lib 全部函数。

    全部方法为静态方法，直接委托给同名的 md_* 函数；另持有 ``parser``
    流式解析器实例（``mdc.feed(byte)`` 逐字节喂入）。

    示例：:

        mdc = MDC()
        uart.write(mdc.motor_ctrl(100, 0, 0, 0))   # 打包 0x31 控制帧
        for b in uart.read(64):
            r = mdc.feed(b)                        # 流式解析
            if r and r[0] == 0xF0:
                st = mdc.parse_status(r[1])
    """

    def __init__(self, max_data=MD_MAX_DATA):
        """构造 MDC；max_data 传递给内部 MDParser。"""
        self.parser = MDParser(max_data=max_data)

    def feed(self, byte):
        """流式喂字节，同 MDParser.feed。"""
        return self.parser.feed(byte)

    def reset_parser(self):
        """清空流式解析器缓冲。"""
        self.parser.reset()

    # ---- 底层 ----
    crc8 = staticmethod(md_crc8)
    build_frame = staticmethod(md_build_frame)
    parse_frame = staticmethod(md_parse_frame)
    # ---- 文本指令 ----
    text_build = staticmethod(md_text_build)
    version = staticmethod(md_text_version)
    help = staticmethod(md_text_help)
    status = staticmethod(md_text_status)
    check = staticmethod(md_text_check)
    detect = staticmethod(md_text_detect)
    save = staticmethod(md_text_save)
    load = staticmethod(md_text_load)
    reset = staticmethod(md_text_reset)
    enczero = staticmethod(md_text_enczero)
    mode = staticmethod(md_text_mode)
    # ---- 二进制命令 ----
    ping = staticmethod(md_bin_ping)
    read_param = staticmethod(md_bin_read_param)
    write_param = staticmethod(md_bin_write_param)
    write_field = staticmethod(md_bin_write_field)
    save_eeprom = staticmethod(md_bin_save)
    load_eeprom = staticmethod(md_bin_load)
    factory_reset = staticmethod(md_bin_factory_reset)
    motor_raw = staticmethod(md_bin_motor_raw)
    motor_ctrl = staticmethod(md_bin_motor_ctrl)
    motor_jog = staticmethod(md_bin_motor_jog)
    subscribe = staticmethod(md_bin_subscribe)
    unsubscribe = staticmethod(md_bin_unsubscribe)
    debug_sbus = staticmethod(md_bin_debug_sbus)
    debug_speed = staticmethod(md_bin_debug_speed)
    enter_bl = staticmethod(md_bin_enter_bl)
    reboot = staticmethod(md_bin_reboot)
    # ---- 解析 ----
    parse_ack = staticmethod(md_parse_ack)
    parse_status = staticmethod(md_parse_status)
    parse_detect = staticmethod(md_parse_detect)
    parse_sbus = staticmethod(md_parse_sbus)
    parse_config = staticmethod(md_parse_config)
    pack_config = staticmethod(md_pack_config)


if __name__ == "__main__":
    # 直接运行本文件：打印一行自检信息（完整自测见各平台 README「一致性验证」）
    print("mdc_lib: PING 帧 =", " ".join("%02X" % b for b in md_bin_ping()))
    print("mdc_lib: crc8(b\"123456789\") = 0x%02X" % md_crc8(b"123456789"))
