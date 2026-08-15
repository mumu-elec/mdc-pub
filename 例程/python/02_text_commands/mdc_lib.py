# -*- coding: utf-8 -*-
"""
mdc_lib.py — Motor Driver Controller 通用调用库（Python 参考实现）
==================================================================

**定位：** 只负责「打包要发送的数据」与「解析收到的数据」，
串口收发由用户自己实现（拿到返回的 bytes 自行 Serial.write / ser.write；
收到的字节喂给解析函数或 MDParser 流式解析器）。

**协议依据：** `../例程/common/协议规范.md`（布局 v2.1，config_t=231B，
24 条文本指令，18 条二进制命令）
**API 依据：** `../API.md`（统一 API 规范，本文件为其 Python 实现，
函数命名 / 参数顺序 / 返回约定与该文档逐一对应）

**特性：**
    * 纯标准库（struct / collections / math），零第三方依赖
    * 函数式 API：`md_*` 前缀，与 C / C++ / MicroPython 等平台同一套签名约定
    * `MDC` 便捷类：把全部函数封装为方法，另含流式解析器实例
    * `MDParser` 流式解析器：逐字节喂入，自动找 0xAA 同步 + CRC8 校验，
      容忍串口噪声；与二进制帧混流的文本回显行会被当作噪声丢弃

**返回约定（API.md §2.3）：**
    * 打包函数返回 `bytes`；文本函数返回 `bytes`（含 `\\n`，UTF-8）
    * 解析函数返回 `namedtuple` / `dict`；参数非法抛 `ValueError`

**用法示例（用户实现串口收发）：**

    import serial
    ser = serial.Serial("COM5", 2000000)            # USB 虚拟串口 2000000-8N1

    # ① 打包 → 发送
    ser.write(md_bin_motor_ctrl(100, 0, 0, 0))      # 0x31 四通道批量控制
    ser.write(md_text_mode(1, "speed"))             # 文本指令：/mode 1 speed\\n

    # ② 接收 → 流式解析
    parser = MDParser()
    for b in ser.read(64):
        r = parser.feed(b)                          # 完整且 CRC 通过的一帧返回 (cmd, payload)
        if r and r[0] == 0xF0:                      # 0xF0 STATUS_REPORT
            st = md_parse_status(r[1])
            print(st.rpm)
"""

import math
import struct
import sys
from collections import namedtuple
from typing import Optional

# ---------------------------------------------------------------------------
# 常量（API.md §2.5，所有平台一致）
# ---------------------------------------------------------------------------
MD_SYNC = 0xAA            # 二进制帧同步字
MD_MAX_DATA = 250         # DATA 段最大长度
MD_CONFIG_SIZE = 231      # config_t 大小
MD_FRAME_MAX = 235        # 最大整帧长度（4 + 231）
MD_CRC8_POLY = 0x07       # CRC8 多项式
MD_CMD_PING = 0x01        # 二进制命令号（全表见 §5）
MD_ERR_OK = 0x00          # ACK 成功
MD_ERR_FAIL = 0xFF        # ACK 失败
MD_PARSER_BUF = 256       # 流式解析器缓冲上限（默认，可裁剪）

# 二进制命令字（协议规范 §3.3，18 条）
MD_CMD_READ_PARAM = 0x10
MD_CMD_WRITE_PARAM = 0x11
MD_CMD_WRITE_FIELD = 0x12
MD_CMD_SAVE_EEPROM = 0x20
MD_CMD_LOAD_EEPROM = 0x21
MD_CMD_FACTORY_RESET = 0x22
MD_CMD_MOTOR_RAW = 0x30
MD_CMD_MOTOR_CTRL = 0x31
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
    "MD_CMD_MOTOR_RAW", "MD_CMD_MOTOR_CTRL", "MD_CMD_SUBSCRIBE",
    "MD_CMD_UNSUBSCRIBE", "MD_CMD_DEBUG_SBUS", "MD_CMD_DEBUG_SPEED",
    "MD_CMD_ENTER_BL", "MD_CMD_REBOOT", "MD_CMD_STATUS_REPORT",
    "MD_CMD_DETECT_REPORT", "MD_CMD_SBUS_DATA",
    "md_crc8", "md_build_frame", "md_parse_frame", "MDParser",
    "md_text_build", "md_text_version", "md_text_help", "md_text_status",
    "md_text_check", "md_text_detect", "md_text_save", "md_text_load",
    "md_text_reset", "md_text_enczero", "md_text_mode",
    "md_bin_ping", "md_bin_read_param", "md_bin_write_param",
    "md_bin_write_field", "md_bin_save", "md_bin_load", "md_bin_factory_reset",
    "md_bin_motor_raw", "md_bin_motor_ctrl", "md_bin_subscribe",
    "md_bin_unsubscribe", "md_bin_debug_sbus", "md_bin_debug_speed",
    "md_bin_enter_bl", "md_bin_reboot",
    "md_parse_ack", "md_parse_status", "md_parse_detect", "md_parse_sbus",
    "md_parse_config", "md_pack_config",
    "Frame", "Ack", "md_status_t", "Detect", "Sbus", "MDC",
]


# ---------------------------------------------------------------------------
# 内部工具
# ---------------------------------------------------------------------------
def _as_bytes(value, what):
    """把 bytes / bytearray / 0~255 整数列表归一化为 bytes；其余类型抛 ValueError。"""
    if isinstance(value, bytearray):
        return bytes(value)
    if isinstance(value, bytes):
        return value
    if isinstance(value, (list, tuple)):
        out = bytearray()
        for b in value:
            if isinstance(b, bool) or not isinstance(b, int) or not 0 <= b <= 0xFF:
                raise ValueError("%s 元素需要 0~255 的整数，实际 %r" % (what, b))
            out.append(b)
        return bytes(out)
    raise ValueError("%s 需要 bytes/bytearray（或整数列表），实际 %r"
                     % (what, type(value).__name__))


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
    if not math.isfinite(v):
        raise ValueError("%s 需要有限数值，实际 %r" % (name, value))
    try:
        struct.pack("<f", v)          # f32 溢出会抛 struct.error / OverflowError
    except (struct.error, OverflowError):
        raise ValueError("%s 超出 f32 可表示范围：%r" % (name, value))
    return v


def _check_list(value, count, name):
    """校验长度为 count 的列表/元组。"""
    if not isinstance(value, (list, tuple)) or len(value) != count:
        raise ValueError("%s 需要 %d 个元素，实际 %r" % (name, count, value))
    return value


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
    _check_list(kp, 4, "kp"); _check_list(ki, 4, "ki")
    _check_list(kd, 4, "kd"); _check_list(ilim, 4, "ilim")
    seq = []
    for ch in range(4):
        seq += [_check_float(kp[ch], "kp"),
                _check_float(ki[ch], "ki"),
                _check_float(kd[ch], "kd"),
                _check_float(ilim[ch], "ilim")]
    struct.pack_into("<16f", raw, offset, *seq)


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
    :param data: DATA 段（bytes/bytearray，长度 ≤ MD_MAX_DATA=250）
    :raises ValueError: cmd 越界或 data 超长
    """
    cmd = _check_int(cmd, 0, 0xFF, "cmd")
    data = _as_bytes(data, "data")
    if len(data) > MD_MAX_DATA:
        raise ValueError("DATA 长度 %d 超过上限 %d" % (len(data), MD_MAX_DATA))
    body = bytes((cmd, len(data))) + data
    return bytes((MD_SYNC,)) + body + bytes((md_crc8(body),))


# 帧级解析结果：cmd=命令字，payload=DATA 段，valid=是否有效
Frame = namedtuple("Frame", ["cmd", "payload", "valid"])


def md_parse_frame(frame):
    """帧级解析：校验 SYNC 与 CRC，返回 ``Frame(cmd, payload, valid)`` namedtuple。

    * valid=True：cmd = 命令字，payload = DATA 段 bytes（长度 = LEN）
    * valid=False：帧无效（SYNC 不符 / 长度不符 / CRC 失败），cmd/payload 无意义

    与 C 版 `int md_parse_frame(...)` 返回 valid 标志的语义一致：
    无效帧不抛异常（字节流中出现坏帧是正常情况），仅输入类型非法抛 ValueError。
    """
    frame = _as_bytes(frame, "frame")
    if len(frame) < 4 or frame[0] != MD_SYNC:
        return Frame(cmd=0, payload=b"", valid=False)
    plen = frame[2]
    if plen > MD_MAX_DATA or len(frame) != 4 + plen:
        return Frame(cmd=0, payload=b"", valid=False)
    body = frame[1:3 + plen]                      # CMD + LEN + DATA
    if md_crc8(body) != frame[3 + plen]:
        return Frame(cmd=0, payload=b"", valid=False)
    return Frame(cmd=frame[1], payload=frame[3:3 + plen], valid=True)


class MDParser:
    """流式解析器：逐字节喂入，自动找 0xAA 同步 + CRC8 校验。

    行为（API.md §3.4）：
        * 滑动窗口找 0xAA；LEN > max_data（默认 250）时丢弃该同步字重扫
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

    def feed(self, byte: int) -> Optional[tuple]:
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
def md_text_build(cmd: str, args: Optional[str] = None) -> bytes:
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


def md_text_version() -> bytes:
    """``/version\\n``：显示硬件/软件版本。"""
    return md_text_build("/version")


def md_text_help() -> bytes:
    """``/help\\n``：打印所有可用命令。"""
    return md_text_build("/help")


def md_text_status() -> bytes:
    """``/status\\n``：打印全部配置参数。"""
    return md_text_build("/status")


def md_text_check() -> bytes:
    """``/check\\n``：显示实时状态（编码器/RPM/输出/运行时间）。"""
    return md_text_build("/check")


def md_text_detect() -> bytes:
    """``/detect\\n``：重新检测 USART2 工作模式/波特率。"""
    return md_text_build("/detect")


def md_text_save() -> bytes:
    """``/save\\n``：当前 RAM 配置写入 EEPROM。"""
    return md_text_build("/save")


def md_text_load() -> bytes:
    """``/load\\n``：从 EEPROM 重新加载配置到 RAM。"""
    return md_text_build("/load")


def md_text_reset() -> bytes:
    """``/reset\\n``：恢复出厂默认值。"""
    return md_text_build("/reset")


def md_text_enczero(ch: int) -> bytes:
    """``/enczero <ch>\\n``：清零指定通道（1~4）编码器累计计数。"""
    ch = _check_int(ch, 1, 4, "ch")
    return md_text_build("/enczero", str(ch))


def md_text_mode(ch: int, mode: Optional[str] = None) -> bytes:
    """``/mode <ch> [mode]\\n``：设置通道控制模式。

    * ``md_text_mode(1)``          -> ``b"/mode 1\\n"``（省略 = 读取模式）
    * ``md_text_mode(1, "speed")`` -> ``b"/mode 1 speed\\n"``
    * mode 取值：open / speed / pos。
    """
    ch = _check_int(ch, 1, 4, "ch")
    return md_text_build("/mode", str(ch) if mode is None else "%d %s" % (ch, mode))


# ---------------------------------------------------------------------------
# §5 二进制命令层（15 个打包函数，返回整帧字节）
# ---------------------------------------------------------------------------
def md_bin_ping() -> bytes:
    """0x01 PING：连通性测试。验证向量：``== b"\\xAA\\x01\\x00\\x15"``。"""
    return md_build_frame(MD_CMD_PING)


def md_bin_read_param() -> bytes:
    """0x10 READ_PARAM：读取全部配置（应答为 config_t 231B）。"""
    return md_build_frame(MD_CMD_READ_PARAM)


def md_bin_write_param(cfg) -> bytes:
    """0x11 WRITE_PARAM：写入全部配置（仅 RAM，受保护字段自动还原）。

    :param cfg: 231B bytes（config_t），或字段 dict（自动经 md_pack_config 打包）
    """
    if isinstance(cfg, dict):
        data = md_pack_config(cfg)
    else:
        data = _as_bytes(cfg, "cfg")
        if len(data) != MD_CONFIG_SIZE:
            raise ValueError("WRITE_PARAM 的 config_t 应为 %dB，实际 %dB"
                             % (MD_CONFIG_SIZE, len(data)))
    return md_build_frame(MD_CMD_WRITE_PARAM, data)


def md_bin_write_field(field_id: int, value, value_len: Optional[int] = None) -> bytes:
    """0x12 WRITE_FIELD：按偏移量写入单个字段。

    DATA 布局：``[field_id:2B LE][value:nB]``。
    * value 为 bytes/bytearray 时 value_len 缺省取 len(value)
    * value 为 int 时必须给 value_len（1/2/4），按小端打包

    注意：固件拒绝 offset < 12（受保护区），库只负责打包，不代做该检查。
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
        raw = value.to_bytes(value_len, "little")
    else:
        raise ValueError("value 需要 bytes/bytearray（或 int + value_len）")
    data = struct.pack("<H", field_id) + raw
    return md_build_frame(MD_CMD_WRITE_FIELD, data)


def md_bin_save() -> bytes:
    """0x20 SAVE_EEPROM：RAM 配置写入 EEPROM（约 190ms）。"""
    return md_build_frame(MD_CMD_SAVE_EEPROM)


def md_bin_load() -> bytes:
    """0x21 LOAD_EEPROM：从 EEPROM 加载到 RAM。"""
    return md_build_frame(MD_CMD_LOAD_EEPROM)


def md_bin_factory_reset() -> bytes:
    """0x22 FACTORY_RESET：恢复出厂默认（RAM+EEPROM）。"""
    return md_build_frame(MD_CMD_FACTORY_RESET)


def md_bin_motor_raw(ch: int, dir_: int, pwm: int) -> bytes:
    """0x30 MOTOR_RAW：单通道 PWM 直驱（旁路 PID，受优先级仲裁）。

    DATA：``[ch:1B][dir:1B][pwm:2B LE]``；ch=0~3，dir=0 正转/1 反转，pwm=0~1000。
    """
    ch = _check_int(ch, 0, 3, "ch")
    dir_ = _check_int(dir_, 0, 1, "dir")
    pwm = _check_int(pwm, 0, 1000, "pwm")
    return md_build_frame(MD_CMD_MOTOR_RAW, bytes((ch, dir_)) + struct.pack("<H", pwm))


def md_bin_motor_ctrl(t0: int, t1: int, t2: int, t3: int) -> bytes:
    """0x31 MOTOR_CTRL：四通道批量控制（核心控制帧，int32 LE，受优先级仲裁）。

    目标值含义随各通道控制模式：开环 = PWM（±1000）、速度 = RPM、
    位置 = 0.1°（±3600 = ±360.0°）。支持负数。
    验证向量：``md_bin_motor_ctrl(100, -200, 0, 300)`` 的 DATA 段 ==
    ``64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00``。
    """
    vals = [_check_int(v, -0x80000000, 0x7FFFFFFF, "t%d" % i)
            for i, v in enumerate((t0, t1, t2, t3))]
    return md_build_frame(MD_CMD_MOTOR_CTRL, struct.pack("<4i", *vals))


def md_bin_subscribe(interval_ms: int) -> bytes:
    """0x40 SUBSCRIBE：开启状态周期上报。

    DATA：``[interval_ms:2B LE]``。固件钳位 ≥20ms（库不代做该限制）。
    """
    interval_ms = _check_int(interval_ms, 0, 0xFFFF, "interval_ms")
    return md_build_frame(MD_CMD_SUBSCRIBE, struct.pack("<H", interval_ms))


def md_bin_unsubscribe() -> bytes:
    """0x41 UNSUBSCRIBE：关闭状态上报。"""
    return md_build_frame(MD_CMD_UNSUBSCRIBE)


def md_bin_debug_sbus(enable) -> bytes:
    """0x43 DEBUG_SBUS：SBUS 16 通道实时上报开关（enable 为真值/0/1）。"""
    return md_build_frame(MD_CMD_DEBUG_SBUS, b"\x01" if enable else b"\x00")


def md_bin_debug_speed(enable) -> bytes:
    """0x44 DEBUG_SPEED：速度原始值上报开关（开启后 STATUS_REPORT 帧 72B）。"""
    return md_build_frame(MD_CMD_DEBUG_SPEED, b"\x01" if enable else b"\x00")


def md_bin_enter_bl() -> bytes:
    """0x52 ENTER_BL：软复位进入 Bootloader（写 RTC magic + SystemReset）。"""
    return md_build_frame(MD_CMD_ENTER_BL)


def md_bin_reboot() -> bytes:
    """0x53 REBOOT：系统重启（ACK 后延迟 100ms）。"""
    return md_build_frame(MD_CMD_REBOOT)


# ---------------------------------------------------------------------------
# §6 解析层
# ---------------------------------------------------------------------------
# ACK 解析结果：cmd=命令字（DATA 段输入时为 None），err=错误码（0 成功，非 0 失败）
Ack = namedtuple("Ack", ["cmd", "err"])

# STATUS_REPORT 解析结果（§6.2）：enc/tgt/rpm/rpm_raw 各 4 元素；extended=1 表示 72B 扩展
md_status_t = namedtuple("md_status_t",
                         ["enc", "tgt", "rpm", "rpm_raw",
                          "sbus_frame_cnt", "sbus_ok_cnt", "extended"])

# DETECT_REPORT 解析结果：proto=0 失败 1=SBUS 2=UART 3=ELRS
Detect = namedtuple("Detect", ["proto", "inv", "baud"])

# SBUS_DATA 解析结果：ch = 16 个通道值（uint16 LE）
Sbus = namedtuple("Sbus", ["ch"])


def md_parse_ack(payload) -> Ack:
    """解析 ACK 帧的 DATA 段（1 字节 err），返回 ``Ack(cmd, err)`` namedtuple。

    * err=0x00 成功；任何非 0 均视为失败（兼容固件个别场景 0x01/0x02）
    * 输入为 1 字节 DATA 段时 cmd=None（命令字由帧头提供，调用方已知）；
      也接受完整 ACK 帧（长度≥4：``[SYNC][CMD][0x01][err][CRC]``），自动取 cmd。

    :raises ValueError: 输入长度既不是 1 也不是 ≥4
    """
    payload = _as_bytes(payload, "payload")
    if len(payload) == 1:
        return Ack(cmd=None, err=payload[0])
    if len(payload) >= 4:
        return Ack(cmd=payload[1], err=payload[3])
    raise ValueError("ACK 载荷需要 1 字节 err（或完整 ACK 帧），实际 %d 字节"
                     % len(payload))


def md_parse_status(payload) -> md_status_t:
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
        enc = struct.unpack_from("<4i", payload, 0)
        tgt = struct.unpack_from("<4f", payload, 16)
        rpm = struct.unpack_from("<4i", payload, 32)
        frame_cnt, ok_cnt = struct.unpack_from("<II", payload, 48)
        rpm_raw = (0, 0, 0, 0)
        extended = 0
    elif n == 72:
        enc = struct.unpack_from("<4i", payload, 0)
        tgt = struct.unpack_from("<4f", payload, 16)
        rpm = struct.unpack_from("<4i", payload, 32)
        rpm_raw = struct.unpack_from("<4i", payload, 48)
        frame_cnt, ok_cnt = struct.unpack_from("<II", payload, 64)
        extended = 1
    else:
        raise ValueError("未知 STATUS_REPORT 长度：%d（应为 56 或 72）" % n)
    return md_status_t(enc=enc, tgt=tgt, rpm=rpm, rpm_raw=rpm_raw,
                       sbus_frame_cnt=frame_cnt, sbus_ok_cnt=ok_cnt,
                       extended=extended)


def md_parse_detect(payload) -> Detect:
    """解析 0xF1 DETECT_REPORT：``[proto:1B][inv:1B][baud:4B LE]``。

    proto：0=识别失败（inv/baud 无效），1=SBUS，2=UART，3=ELRS。
    """
    payload = _as_bytes(payload, "payload")
    if len(payload) != 6:
        raise ValueError("DETECT_REPORT 载荷应为 6B，实际 %d 字节" % len(payload))
    proto, inv = payload[0], payload[1]
    baud = struct.unpack_from("<I", payload, 2)[0]
    return Detect(proto=proto, inv=inv, baud=baud)


def md_parse_sbus(payload) -> Sbus:
    """解析 0xF2 SBUS_DATA：``[ch0~15:16×uint16 LE]``，返回 ``Sbus(ch)``。"""
    payload = _as_bytes(payload, "payload")
    if len(payload) != 32:
        raise ValueError("SBUS_DATA 载荷应为 32B，实际 %d 字节" % len(payload))
    return Sbus(ch=struct.unpack_from("<16H", payload, 0))


# ---------------------------------------------------------------------------
# §6.5 config_t：解析 / 打包（协议规范 §5 偏移表，布局 v2.1，231B）
# ---------------------------------------------------------------------------
# md_pack_config 缺省字段的中性值：数值 0；遥控通道映射默认 CH1（存 0）
_CONFIG_DEFAULTS = {
    "baud_rate": 0, "cmd_timeout_ms": 0,
    "protocol": 0, "sbus_inv": 0, "ctrl_priority": 0,
    "control_mode": [0, 0, 0, 0], "motor_invert": [0, 0, 0, 0],
    "encoder_cpr": [0, 0, 0, 0], "speed_period_ms": [0, 0, 0, 0],
    "speed_pid_type": [0, 0, 0, 0], "speed_olim": [0, 0, 0, 0],
    "speed_kp": [0.0] * 4, "speed_ki": [0.0] * 4,
    "speed_kd": [0.0] * 4, "speed_ilim": [0.0] * 4,
    "pos_period_ms": [0, 0, 0, 0], "pos_pid_type": [0, 0, 0, 0],
    "pos_kp": [0.0] * 4, "pos_ki": [0.0] * 4,
    "pos_kd": [0.0] * 4, "pos_ilim": [0.0] * 4,
    "pos_olim": [0.0] * 4, "pos_angle_cpr": [0, 0, 0, 0],
    "speed_filter_type": [0, 0, 0, 0], "speed_filter_window": [0, 0, 0, 0],
    "sbus_channel": [1, 1, 1, 1], "rc_dir_ch": [1, 1, 1, 1],
    "rc_map_mode": [0, 0, 0, 0], "rc_dir_en": [0, 0, 0, 0],
    "sbus_param": [0, 0, 0, 0], "sbus_range_min": 0, "sbus_range_max": 0,
}


def md_parse_config(raw) -> dict:
    """解析 231B config_t → dict（键名与 API.md §6.5 字段一一对应）。

    位域按通道解码（API.md §6.5 位域解析约定）：
        * control_mode / motor_invert：每电机 2bit（``(raw[18] >> (ch*2)) & 0x03``）
        * speed_pid_type / pos_pid_type / speed_filter_type：每电机 4bit
        * sbus_channel / rc_dir_ch：每电机 4bit，存储值 0~15 表示 CH1~16，解析 +1
        * rc_map_mode（bit0-3）/ rc_dir_en（bit4-7）：每电机 1bit
        * protocol（bit0-3）/ sbus_inv（bit4）/ ctrl_priority（bit5）：来自 comm_flags

    :raises ValueError: 长度不是 231B
    """
    raw = _as_bytes(raw, "raw")
    if len(raw) != MD_CONFIG_SIZE:
        raise ValueError("config_t 应为 %dB，实际 %dB" % (MD_CONFIG_SIZE, len(raw)))

    out = {}
    # 通讯（offset 11/15/17）
    out["baud_rate"] = struct.unpack_from("<I", raw, 11)[0]
    out["cmd_timeout_ms"] = struct.unpack_from("<H", raw, 15)[0]
    flags = raw[17]                               # comm_flags
    out["protocol"] = flags & 0x0F
    out["sbus_inv"] = (flags >> 4) & 0x01
    out["ctrl_priority"] = (flags >> 5) & 0x01
    # 电机×4（offset 18/19）
    cm = raw[18]                                  # control_mode
    out["control_mode"] = [(cm >> (ch * 2)) & 0x03 for ch in range(4)]
    mi = raw[19]                                  # motor_invert
    out["motor_invert"] = [(mi >> (ch * 2)) & 0x03 for ch in range(4)]
    # 速度环（offset 20~109）
    out["encoder_cpr"] = list(struct.unpack_from("<4H", raw, 20))
    out["speed_period_ms"] = list(struct.unpack_from("<4H", raw, 28))
    spt = struct.unpack_from("<H", raw, 36)[0]    # speed_pid_type
    out["speed_pid_type"] = [(spt >> (ch * 4)) & 0x0F for ch in range(4)]
    out["speed_olim"] = list(struct.unpack_from("<4H", raw, 38))
    sp = struct.unpack_from("<16f", raw, 46)      # speed_ctrl_params
    out["speed_kp"] = list(sp[0::4])
    out["speed_ki"] = list(sp[1::4])
    out["speed_kd"] = list(sp[2::4])
    out["speed_ilim"] = list(sp[3::4])
    # 位置环（offset 110~207）
    out["pos_period_ms"] = list(struct.unpack_from("<4H", raw, 110))
    ppt = struct.unpack_from("<H", raw, 118)[0]   # pos_pid_type
    out["pos_pid_type"] = [(ppt >> (ch * 4)) & 0x0F for ch in range(4)]
    pp = struct.unpack_from("<16f", raw, 120)     # pos_ctrl_params
    out["pos_kp"] = list(pp[0::4])
    out["pos_ki"] = list(pp[1::4])
    out["pos_kd"] = list(pp[2::4])
    out["pos_ilim"] = list(pp[3::4])
    out["pos_olim"] = list(struct.unpack_from("<4f", raw, 184))
    out["pos_angle_cpr"] = list(struct.unpack_from("<4H", raw, 200))
    # 滤波 / 遥控（offset 208~230）
    sft = struct.unpack_from("<H", raw, 208)[0]   # speed_filter_type
    out["speed_filter_type"] = [(sft >> (ch * 4)) & 0x0F for ch in range(4)]
    out["speed_filter_window"] = list(struct.unpack_from("<4B", raw, 210))
    scp = struct.unpack_from("<H", raw, 214)[0]   # sbus_channel_pack
    out["sbus_channel"] = [((scp >> (ch * 4)) & 0x0F) + 1 for ch in range(4)]
    rdc = struct.unpack_from("<H", raw, 216)[0]   # rc_dir_ch
    out["rc_dir_ch"] = [((rdc >> (ch * 4)) & 0x0F) + 1 for ch in range(4)]
    rmm = raw[218]                                # rc_map_mode
    out["rc_map_mode"] = [(rmm >> ch) & 0x01 for ch in range(4)]
    out["rc_dir_en"] = [(rmm >> (4 + ch)) & 0x01 for ch in range(4)]
    out["sbus_param"] = list(struct.unpack_from("<4H", raw, 219))
    out["sbus_range_min"] = struct.unpack_from("<H", raw, 227)[0]
    out["sbus_range_max"] = struct.unpack_from("<H", raw, 229)[0]
    return out


def md_pack_config(cfg: dict) -> bytes:
    """把配置 dict 打包为 231B config_t bytes（协议规范 §5 偏移表）。

    * 键名与 md_parse_config 一致；缺省字段取中性值（数值 0，通道映射默认 CH1），
      因此可用 ``md_parse_config`` 的结果直接回写（往返无损）
    * 受保护区（offset 0~10：magic/hw_ver/sw_ver/reserved/crc）恒为 0，
      固件写入时自动还原受保护字段
    * 位域打包与 API.md §6.5 约定一致：sbus_channel / rc_dir_ch 的 API 值
      1~16 存为 0~15

    :raises ValueError: 字段值越界 / 类型非法；未知键抛 KeyError
    """
    if not isinstance(cfg, dict):
        raise ValueError("cfg 需要 dict，实际 %r" % type(cfg).__name__)
    unknown = set(cfg) - set(_CONFIG_DEFAULTS)
    if unknown:
        raise KeyError("未知配置字段: %s（可用: %s）"
                       % (", ".join(sorted(unknown)), ", ".join(sorted(_CONFIG_DEFAULTS))))
    d = dict(_CONFIG_DEFAULTS)
    d.update(cfg)

    raw = bytearray(MD_CONFIG_SIZE)               # 受保护区 offset 0~10 保持 0
    # 通讯
    struct.pack_into("<I", raw, 11, _check_int(d["baud_rate"], 0, 0xFFFFFFFF, "baud_rate"))
    struct.pack_into("<H", raw, 15, _check_int(d["cmd_timeout_ms"], 0, 0xFFFF, "cmd_timeout_ms"))
    flags = ((_check_int(d["protocol"], 0, 15, "protocol") & 0x0F)
             | ((_check_int(d["sbus_inv"], 0, 1, "sbus_inv") & 0x01) << 4)
             | ((_check_int(d["ctrl_priority"], 0, 1, "ctrl_priority") & 0x01) << 5))
    raw[17] = flags
    # 电机位域
    raw[18] = _pack_2bit(d["control_mode"], "control_mode")
    raw[19] = _pack_2bit(d["motor_invert"], "motor_invert")
    # 速度环
    struct.pack_into("<4H", raw, 20,
                     *[_check_int(v, 0, 0xFFFF, "encoder_cpr") for v in _check_list(d["encoder_cpr"], 4, "encoder_cpr")])
    struct.pack_into("<4H", raw, 28,
                     *[_check_int(v, 0, 0xFFFF, "speed_period_ms") for v in _check_list(d["speed_period_ms"], 4, "speed_period_ms")])
    struct.pack_into("<H", raw, 36, _pack_4bit(d["speed_pid_type"], "speed_pid_type"))
    struct.pack_into("<4H", raw, 38,
                     *[_check_int(v, 0, 0xFFFF, "speed_olim") for v in _check_list(d["speed_olim"], 4, "speed_olim")])
    _pack_float16(raw, 46, d["speed_kp"], d["speed_ki"], d["speed_kd"], d["speed_ilim"])
    # 位置环
    struct.pack_into("<4H", raw, 110,
                     *[_check_int(v, 0, 0xFFFF, "pos_period_ms") for v in _check_list(d["pos_period_ms"], 4, "pos_period_ms")])
    struct.pack_into("<H", raw, 118, _pack_4bit(d["pos_pid_type"], "pos_pid_type"))
    _pack_float16(raw, 120, d["pos_kp"], d["pos_ki"], d["pos_kd"], d["pos_ilim"])
    struct.pack_into("<4f", raw, 184,
                     *[_check_float(v, "pos_olim") for v in _check_list(d["pos_olim"], 4, "pos_olim")])
    struct.pack_into("<4H", raw, 200,
                     *[_check_int(v, 0, 0xFFFF, "pos_angle_cpr") for v in _check_list(d["pos_angle_cpr"], 4, "pos_angle_cpr")])
    # 滤波 / 遥控
    struct.pack_into("<H", raw, 208, _pack_4bit(d["speed_filter_type"], "speed_filter_type"))
    struct.pack_into("<4B", raw, 210,
                     *[_check_int(v, 0, 0xFF, "speed_filter_window") for v in _check_list(d["speed_filter_window"], 4, "speed_filter_window")])
    struct.pack_into("<H", raw, 214, _pack_channel(d["sbus_channel"], "sbus_channel"))
    struct.pack_into("<H", raw, 216, _pack_channel(d["rc_dir_ch"], "rc_dir_ch"))
    rmm = 0
    for ch, v in enumerate(_check_list(d["rc_map_mode"], 4, "rc_map_mode")):
        rmm |= (_check_int(v, 0, 1, "rc_map_mode") & 0x01) << ch
    for ch, v in enumerate(_check_list(d["rc_dir_en"], 4, "rc_dir_en")):
        rmm |= (_check_int(v, 0, 1, "rc_dir_en") & 0x01) << (4 + ch)
    raw[218] = rmm
    struct.pack_into("<4H", raw, 219,
                     *[_check_int(v, 0, 0xFFFF, "sbus_param") for v in _check_list(d["sbus_param"], 4, "sbus_param")])
    struct.pack_into("<H", raw, 227, _check_int(d["sbus_range_min"], 0, 0xFFFF, "sbus_range_min"))
    struct.pack_into("<H", raw, 229, _check_int(d["sbus_range_max"], 0, 0xFFFF, "sbus_range_max"))
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
        ser.write(mdc.motor_ctrl(100, 0, 0, 0))   # 打包 0x31 控制帧
        for b in ser.read(64):
            r = mdc.feed(b)                       # 流式解析
            if r and r[0] == 0xF0:
                st = mdc.parse_status(r[1])
    """

    def __init__(self, max_data: int = MD_MAX_DATA):
        """构造 MDC；max_data 传递给内部 MDParser。"""
        self.parser = MDParser(max_data=max_data)

    # ---- 底层 ----
    @staticmethod
    def crc8(data):
        """同 md_crc8。"""
        return md_crc8(data)

    @staticmethod
    def build_frame(cmd, data=b""):
        """同 md_build_frame。"""
        return md_build_frame(cmd, data)

    @staticmethod
    def parse_frame(frame):
        """同 md_parse_frame。"""
        return md_parse_frame(frame)

    def feed(self, byte):
        """流式喂字节，同 MDParser.feed。"""
        return self.parser.feed(byte)

    def reset_parser(self):
        """清空流式解析器缓冲。"""
        self.parser.reset()

    # ---- 文本指令 ----
    @staticmethod
    def text_build(cmd, args=None):
        """同 md_text_build。"""
        return md_text_build(cmd, args)

    @staticmethod
    def version():
        """同 md_text_version。"""
        return md_text_version()

    @staticmethod
    def help():
        """同 md_text_help。"""
        return md_text_help()

    @staticmethod
    def status():
        """同 md_text_status。"""
        return md_text_status()

    @staticmethod
    def check():
        """同 md_text_check。"""
        return md_text_check()

    @staticmethod
    def detect():
        """同 md_text_detect。"""
        return md_text_detect()

    @staticmethod
    def save():
        """同 md_text_save。"""
        return md_text_save()

    @staticmethod
    def load():
        """同 md_text_load。"""
        return md_text_load()

    @staticmethod
    def reset():
        """同 md_text_reset。"""
        return md_text_reset()

    @staticmethod
    def enczero(ch):
        """同 md_text_enczero。"""
        return md_text_enczero(ch)

    @staticmethod
    def mode(ch, mode=None):
        """同 md_text_mode。"""
        return md_text_mode(ch, mode)

    # ---- 二进制命令 ----
    @staticmethod
    def ping():
        """同 md_bin_ping。"""
        return md_bin_ping()

    @staticmethod
    def read_param():
        """同 md_bin_read_param。"""
        return md_bin_read_param()

    @staticmethod
    def write_param(cfg):
        """同 md_bin_write_param。"""
        return md_bin_write_param(cfg)

    @staticmethod
    def write_field(field_id, value, value_len=None):
        """同 md_bin_write_field。"""
        return md_bin_write_field(field_id, value, value_len)

    @staticmethod
    def save_eeprom():
        """同 md_bin_save。"""
        return md_bin_save()

    @staticmethod
    def load_eeprom():
        """同 md_bin_load。"""
        return md_bin_load()

    @staticmethod
    def factory_reset():
        """同 md_bin_factory_reset。"""
        return md_bin_factory_reset()

    @staticmethod
    def motor_raw(ch, dir_, pwm):
        """同 md_bin_motor_raw。"""
        return md_bin_motor_raw(ch, dir_, pwm)

    @staticmethod
    def motor_ctrl(t0, t1, t2, t3):
        """同 md_bin_motor_ctrl。"""
        return md_bin_motor_ctrl(t0, t1, t2, t3)

    @staticmethod
    def subscribe(interval_ms):
        """同 md_bin_subscribe。"""
        return md_bin_subscribe(interval_ms)

    @staticmethod
    def unsubscribe():
        """同 md_bin_unsubscribe。"""
        return md_bin_unsubscribe()

    @staticmethod
    def debug_sbus(enable):
        """同 md_bin_debug_sbus。"""
        return md_bin_debug_sbus(enable)

    @staticmethod
    def debug_speed(enable):
        """同 md_bin_debug_speed。"""
        return md_bin_debug_speed(enable)

    @staticmethod
    def enter_bl():
        """同 md_bin_enter_bl。"""
        return md_bin_enter_bl()

    @staticmethod
    def reboot():
        """同 md_bin_reboot。"""
        return md_bin_reboot()

    # ---- 解析 ----
    @staticmethod
    def parse_ack(payload):
        """同 md_parse_ack。"""
        return md_parse_ack(payload)

    @staticmethod
    def parse_status(payload):
        """同 md_parse_status。"""
        return md_parse_status(payload)

    @staticmethod
    def parse_detect(payload):
        """同 md_parse_detect。"""
        return md_parse_detect(payload)

    @staticmethod
    def parse_sbus(payload):
        """同 md_parse_sbus。"""
        return md_parse_sbus(payload)

    @staticmethod
    def parse_config(raw):
        """同 md_parse_config。"""
        return md_parse_config(raw)

    @staticmethod
    def pack_config(cfg):
        """同 md_pack_config。"""
        return md_pack_config(cfg)


if __name__ == "__main__":
    # 直接运行本文件：打印一行自检信息（完整自测请运行 test_mdc_lib.py）
    for _s in (sys.stdout, sys.stderr):
        try:
            _s.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    print("mdc_lib: PING 帧 =", md_bin_ping().hex(" "))
    print("mdc_lib: crc8(b\"123456789\") = 0x%02X" % md_crc8(b"123456789"))
