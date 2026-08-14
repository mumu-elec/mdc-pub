#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
06_config_manager — 配置读写工具（config_t 231B）
==================================================
按协议规范 §5 的 config_t 偏移表实现全字段解析/打包，提供：

    dump                        读取并打印全部可读字段（通道数组逐通道显示）
    set <field[:ch]> <值...>    修改单字段（READ_PARAM → 改 → WRITE_PARAM 写回）
    field <名称|偏移> <值>      演示 WRITE_FIELD 按偏移写单个字段（按类型打包）
    save                        SAVE_EEPROM 持久化
    （无子命令）                 交互模式：菜单选择 dump / set / save

通道字段语法：`字段名:通道1~4`，例如 `speed_olim:1 800`、`mode:2 speed`。

用法：
    python config_manager.py dump
    python config_manager.py --port COM5 set encoder_cpr:1 500
    python config_manager.py set mode:1 speed
    python config_manager.py field speed_olim 800
    python config_manager.py save
    python config_manager.py
"""

import argparse
import struct
import sys

import serial

BAUDRATE = 2000000
CONFIG_SIZE = 231


def setup_console():
    """控制台输出统一 UTF-8（兼容 GBK / 输出重定向场景）。"""
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass

# 二进制帧常量（协议规范 §3）
SYNC = 0xAA
CMD_READ_PARAM = 0x10
CMD_WRITE_PARAM = 0x11
CMD_WRITE_FIELD = 0x12
CMD_SAVE_EEPROM = 0x20
ACK_OK = 0x00

# ── 字段类型打包 ─────────────────────────────────────────
FMT_LETTER = {"u8": "B", "u16": "H", "u32": "I", "f32": "f"}
FMT_SIZE = {"u8": 1, "u16": 2, "u32": 4, "f32": 4}
MODE_NAME = {0: "open(开环)", 1: "speed(速度)", 2: "pos(位置)"}

# ── config_t 字段表（协议规范 §5：offset/size/类型/说明）──
FIELD_TABLE = [
    # name               offset  fmt   count  desc
    ("baud_rate",          11, "u32", 1,  "USART2 波特率"),
    ("cmd_timeout_ms",     15, "u16", 1,  "指令超时保护 (ms, 0=关闭)"),
    ("comm_flags",         17, "u8",  1,  "bit0-3=protocol(1=SBUS 2=UART 3=ELRS) bit4=sbus_inv bit5=ctrl_priority"),
    ("control_mode",       18, "u8",  1,  "每电机2bit: 0=开环 1=速度 2=位置"),
    ("motor_invert",       19, "u8",  1,  "每电机2bit: bit0=引脚反转 bit1=编码器极性反转"),
    ("encoder_cpr",        20, "u16", 4,  "编码器线数"),
    ("speed_period_ms",    28, "u16", 4,  "速度环周期 (ms)"),
    ("speed_pid_type",     36, "u16", 1,  "每电机4bit: 0=位置式 1=增量式"),
    ("speed_olim",         38, "u16", 4,  "速度环输出限幅 (PWM 0~1000)"),
    ("speed_ctrl_params",  46, "f32", 16, "速度环 Kp/Ki/Kd/Ilim ×4"),
    ("pos_period_ms",     110, "u16", 4,  "位置环周期 (ms)"),
    ("pos_pid_type",      118, "u16", 1,  "每电机4bit: 0=位置式 1=增量式"),
    ("pos_ctrl_params",   120, "f32", 16, "位置环 Kp/Ki/Kd/Ilim ×4"),
    ("pos_olim",          184, "f32", 4,  "位置环输出限幅 (RPM)"),
    ("pos_angle_cpr",     200, "u16", 4,  "位置环转一圈脉冲数 (0=用 encoder_cpr)"),
    ("speed_filter_type", 208, "u16", 1,  "每电机4bit: 0=无 1=滑动平均 2=低通 3=中值"),
    ("speed_filter_window", 210, "u8", 4, "滤波窗口 (MA/中值 1~32, LPF 1~99)"),
    ("sbus_channel_pack", 214, "u16", 1,  "遥控通道映射 (每电机4bit, 0~15=CH1~16)"),
    ("rc_dir_ch",         216, "u16", 1,  "方向映射通道 (每电机4bit)"),
    ("rc_map_mode",       218, "u8",  1,  "bit0-3=映射模式(每电机1bit) bit4-7=方向映射使能(每电机1bit)"),
    ("sbus_param",        219, "u16", 4,  "遥控行程: 开环=最大PWM/速度=最大RPM/位置=最大角度(0.1°)"),
    ("sbus_range_min",    227, "u16", 1,  "通道值下边界"),
    ("sbus_range_max",    229, "u16", 1,  "通道值上边界"),
]
FIELD_BY_NAME = {f[0]: f for f in FIELD_TABLE}
FIELD_BY_OFFSET = {f[1]: f for f in FIELD_TABLE}
ALIASES = {"mode": "control_mode", "cpr": "encoder_cpr"}

# 每通道一个元素（4 元素数组）的字段
CHANNEL_ARRAYS = {name for name, _, _, count, _ in FIELD_TABLE if count == 4}
# 每通道 4 个元素（Kp/Ki/Kd/Ilim）的字段
CTRL_PARAMS = {"speed_ctrl_params", "pos_ctrl_params"}


# ── 协议层（自包含）──────────────────────────────────────
def crc8(data):
    """CRC8-ATM：多项式 0x07，初值 0，范围 CMD+LEN+DATA（不含 SYNC）。"""
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if (c & 0x80) else (c << 1) & 0xFF
    return c


def build_frame(cmd, data=b""):
    body = bytes([cmd, len(data)]) + data
    return bytes([SYNC]) + body + bytes([crc8(body)])


class Driver:
    """最小二进制协议客户端（本工具专用）。"""

    def __init__(self, port):
        self.ser = serial.Serial(port, BAUDRATE, timeout=0.05)
        self.ser.reset_input_buffer()
        self._rx = b""

    def _extract(self):
        buf = self._rx
        while True:
            idx = buf.find(bytes([SYNC]))
            if idx < 0:
                self._rx = b""
                return None
            if idx > 0:
                buf = buf[idx:]
            if len(buf) < 3:
                self._rx = buf
                return None
            ln = buf[2]
            if ln > 250:
                buf = buf[1:]
                continue
            fsize = 3 + ln + 1
            if len(buf) < fsize:
                self._rx = buf
                return None
            if crc8(buf[1:fsize - 1]) == buf[fsize - 1]:
                self._rx = buf[fsize:]
                return (buf[1], buf[3:fsize - 1])
            buf = buf[1:]

    def _read_until(self, want_cmd, want_len=None, timeout=1.0):
        """读取帧直到匹配 want_cmd（可选 want_len），返回 data；超时返回 None。"""
        import time
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            frame = self._extract()
            if frame is None:
                remain = deadline - time.monotonic()
                if remain <= 0:
                    return None
                chunk = self.ser.read(self.ser.in_waiting or 1)
                if chunk:
                    self._rx += chunk
                continue
            c, d = frame
            if c == want_cmd and (want_len is None or len(d) == want_len):
                return d
        return None

    def read_param(self):
        self.ser.write(build_frame(CMD_READ_PARAM))
        data = self._read_until(CMD_READ_PARAM, CONFIG_SIZE)
        if data is None:
            raise RuntimeError("READ_PARAM 超时/数据长度不符")
        return data

    def write_param(self, data):
        self.ser.write(build_frame(CMD_WRITE_PARAM, data))
        return self._ack(CMD_WRITE_PARAM)

    def write_field(self, offset, packed):
        self.ser.write(build_frame(CMD_WRITE_FIELD,
                                   struct.pack("<H", offset) + packed))
        return self._ack(CMD_WRITE_FIELD)

    def save(self):
        self.ser.write(build_frame(CMD_SAVE_EEPROM))
        return self._ack(CMD_SAVE_EEPROM, timeout=1.0)

    def _ack(self, cmd, timeout=0.5):
        data = self._read_until(cmd, 1, timeout)
        if data is None:
            return None
        return data[0]

    def close(self):
        if self.ser.is_open:
            self.ser.close()


# ── 字段解析 / 打包（协议规范 §5）────────────────────────
def _unpack_field(raw, field):
    name, offset, fmt, count, _ = field
    letter = FMT_LETTER[fmt]
    if count == 1:
        return struct.unpack_from("<" + letter, raw, offset)[0]
    return list(struct.unpack_from("<%d%s" % (count, letter), raw, offset))


def parse_config(raw):
    """解析 231B config_t → dict。

    返回：{字段名: 值/列表, '_header': 受保护区信息, '_decoded': 按通道解码的位域}。
    """
    if len(raw) != CONFIG_SIZE:
        raise ValueError(f"config_t 应为 {CONFIG_SIZE}B，实际 {len(raw)}B")
    out = {}
    for f in FIELD_TABLE:
        out[f[0]] = _unpack_field(raw, f)

    # 受保护区 (offset 0~10)：magic + 硬件/固件版本 + reserved + crc
    out["_header"] = {
        "magic": struct.unpack_from("<I", raw, 0)[0],
        "hw_ver": (raw[4], raw[5], raw[6]),      # vA.B.C
        "sw_ver": (raw[7], raw[8]),              # vD.E（D=协议版本）
        "reserved": raw[9],
        "crc": raw[10],
    }

    # 位域按通道解码
    cm = out["control_mode"]
    mi = out["motor_invert"]
    spt = out["speed_pid_type"]
    ppt = out["pos_pid_type"]
    sft = out["speed_filter_type"]
    scp = out["sbus_channel_pack"]
    rdc = out["rc_dir_ch"]
    rmm = out["rc_map_mode"]
    cfl = out["comm_flags"]
    out["_decoded"] = {
        "mode_ch":        [MODE_NAME.get((cm >> (i * 2)) & 0x03, "?") for i in range(4)],
        "motor_inv_ch":   [((mi >> (i * 2)) & 0x03) for i in range(4)],      # bit0=引脚 bit1=编码器
        "speed_pid_type_ch": [(spt >> (i * 4)) & 0x0F for i in range(4)],
        "pos_pid_type_ch":   [(ppt >> (i * 4)) & 0x0F for i in range(4)],
        "filter_type_ch":    [(sft >> (i * 4)) & 0x0F for i in range(4)],
        "sbus_ch_ch":        [(scp >> (i * 4)) & 0x0F for i in range(4)],    # 0~15 = CH1~16
        "rc_dir_ch_ch":      [(rdc >> (i * 4)) & 0x0F for i in range(4)],
        "rc_map_mode_ch":    [((rmm >> i) & 0x01) for i in range(4)],
        "rc_dir_en_ch":      [((rmm >> (4 + i)) & 0x01) for i in range(4)],
        "comm_protocol":  cfl & 0x0F,
        "comm_sbus_inv": (cfl >> 4) & 0x01,
        "comm_priority": (cfl >> 5) & 0x01,
    }
    return out


def _pack_scalar(fmt, value):
    letter = FMT_LETTER[fmt]
    lo, hi = _range_of(fmt)
    if not lo <= value <= hi:
        raise ValueError(f"值 {value} 超出 {fmt} 范围 [{lo}, {hi}]")
    return struct.pack("<" + letter, int(value) if fmt != "f32" else float(value))


def _range_of(fmt):
    return {"u8": (0, 0xFF), "u16": (0, 0xFFFF), "u32": (0, 0xFFFFFFFF),
            "f32": (-3.4e38, 3.4e38)}[fmt]


def pack_config(fields, base=None):
    """将字段字典打包为 231B bytes。

    fields: {字段名: 值 或 列表}。数组字段需提供完整长度列表。
    base: 基准字节流（推荐 READ_PARAM 结果）；缺省未指定字段为 0。
    """
    raw = bytearray(base if base is not None else bytes(CONFIG_SIZE))
    if len(raw) != CONFIG_SIZE:
        raise ValueError(f"base 长度应为 {CONFIG_SIZE}B")
    for name, val in fields.items():
        if name not in FIELD_BY_NAME:
            raise KeyError(f"未知字段: {name}（可用: {', '.join(FIELD_BY_NAME)}）")
        _, offset, fmt, count, _ = FIELD_BY_NAME[name]
        if count == 1:
            raw[offset:offset + FMT_SIZE[fmt]] = _pack_scalar(fmt, val)
        else:
            if not isinstance(val, (list, tuple)) or len(val) != count:
                raise ValueError(f"字段 {name} 需要 {count} 个值，实际 {val}")
            packed = b"".join(_pack_scalar(fmt, v) for v in val)
            raw[offset:offset + len(packed)] = packed
    return bytes(raw)


# ── 字段修改辅助（set 命令）──────────────────────────────
def parse_value(text, fmt):
    """按字段类型解析字符串值。"""
    if fmt == "f32":
        return float(text)
    return int(text, 0)          # 支持 0x 前缀


def parse_mode_word(text):
    """control_mode 支持 open/speed/pos 单词。"""
    t = text.strip().lower()
    if t in ("open", "o"):
        return 0
    if t in ("speed", "s"):
        return 1
    if t in ("pos", "position", "p"):
        return 2
    return int(t, 0)


def _apply_channel_2bit(raw, offset, ch, val):
    """修改 每电机2bit 打包字段（control_mode / motor_invert）。"""
    if not 0 <= val <= 3:
        raise ValueError("2bit 字段取值 0~3")
    byte = raw[offset]
    byte &= ~(0x03 << (ch * 2))
    byte |= (val & 0x03) << (ch * 2)
    raw[offset] = byte


def _apply_channel_4bit(raw, offset, ch, val):
    """修改 每电机4bit 打包字段（pid_type / filter_type / 遥控通道）。"""
    if not 0 <= val <= 15:
        raise ValueError("4bit 字段取值 0~15")
    word = struct.unpack_from("<H", raw, offset)[0]
    word &= ~(0x0F << (ch * 4))
    word |= (val & 0x0F) << (ch * 4)
    struct.pack_into("<H", raw, offset, word)


def apply_set(raw, field_spec, values):
    """在 raw 上应用 set 修改。

    field_spec: "名称" 或 "名称:通道(1~4)"；values: 解析后的值列表。
    返回修改说明字符串。
    """
    if ":" in field_spec:
        name, ch_txt = field_spec.rsplit(":", 1)
        ch = int(ch_txt) - 1
        if not 0 <= ch <= 3:
            raise ValueError("通道取值 1~4")
    else:
        name, ch = field_spec, None

    name = ALIASES.get(name, name)
    if name not in FIELD_BY_NAME:
        raise KeyError(f"未知字段: {name}（可用: {', '.join(FIELD_BY_NAME)}）")
    field = FIELD_BY_NAME[name]
    _, offset, fmt, count, desc = field

    # control_mode / mode：支持 open/speed/pos 单词
    if name == "control_mode":
        vals = [parse_mode_word(v) for v in values]
        if ch is not None:
            if len(vals) != 1:
                raise ValueError("control_mode:ch 只需 1 个值")
            _apply_channel_2bit(raw, offset, ch, vals[0])
            return f"control_mode 通道{ch + 1} = {MODE_NAME.get(vals[0], vals[0])}"
        if len(vals) != 4:
            raise ValueError("control_mode（不带通道）需 4 个值，对应 4 通道")
        for i, v in enumerate(vals):
            _apply_channel_2bit(raw, offset, i, v)
        return "control_mode 四通道 = " + ", ".join(MODE_NAME.get(v, str(v)) for v in vals)

    # 每电机2bit / 4bit 打包字段
    if name in ("motor_invert",):
        vals = [parse_value(v, fmt) for v in values]
        if ch is not None:
            _apply_channel_2bit(raw, offset, ch, vals[0])
            return f"{name} 通道{ch + 1} = {vals[0]} (bit0=引脚反转 bit1=编码器极性)"
        if len(vals) != 4:
            raise ValueError(f"{name} 需 4 个值")
        for i, v in enumerate(vals):
            _apply_channel_2bit(raw, offset, i, v)
        return f"{name} 四通道 = {vals}"

    if name in ("speed_pid_type", "pos_pid_type", "speed_filter_type",
                "sbus_channel_pack", "rc_dir_ch", "rc_map_mode"):
        vals = [parse_value(v, fmt) for v in values]
        if name == "rc_map_mode" and ch is not None:
            if len(vals) != 1 or vals[0] not in (0, 1):
                raise ValueError("rc_map_mode:ch 取值 0/1（映射模式位）")
            byte = raw[offset]
            if vals[0]:
                byte |= (1 << ch)
            else:
                byte &= ~(1 << ch)
            raw[offset] = byte
            return f"rc_map_mode 通道{ch + 1} 映射模式 = {vals[0]} (0=中心零点 1=min零点)"
        if ch is not None:
            if len(vals) != 1:
                raise ValueError(f"{name}:ch 只需 1 个值")
            _apply_channel_4bit(raw, offset, ch, vals[0])
            return f"{name} 通道{ch + 1} = {vals[0]}"
        if len(vals) != 4:
            raise ValueError(f"{name} 需 4 个值")
        for i, v in enumerate(vals):
            _apply_channel_4bit(raw, offset, i, v)
        return f"{name} 四通道 = {vals}"

    # 普通数组字段（含通道数组与 ctrl_params）
    if count > 1:
        vals = [parse_value(v, fmt) for v in values]
        if name in CTRL_PARAMS and ch is not None:
            if len(vals) != 4:
                raise ValueError(f"{name}:ch 需要 4 个值 (kp ki kd ilim)")
            base_idx = ch * 4
            for j, v in enumerate(vals):
                _pack_scalar_into(raw, offset + (base_idx + j) * FMT_SIZE[fmt], fmt, v)
            return f"{name} 通道{ch + 1} kp/ki/kd/ilim = {vals}"
        if ch is not None:
            if len(vals) != 1:
                raise ValueError(f"{name}:ch 只需 1 个值")
            _pack_scalar_into(raw, offset + ch * FMT_SIZE[fmt], fmt, vals[0])
            return f"{name} 通道{ch + 1} = {vals[0]}"
        if len(vals) != count:
            raise ValueError(f"{name} 需要 {count} 个值，实际 {len(vals)}")
        for j, v in enumerate(vals):
            _pack_scalar_into(raw, offset + j * FMT_SIZE[fmt], fmt, v)
        return f"{name} = {vals}"

    # 标量字段
    vals = [parse_value(v, fmt) for v in values]
    if len(vals) != 1:
        raise ValueError(f"{name} 只需 1 个值")
    _pack_scalar_into(raw, offset, fmt, vals[0])
    return f"{name} = {vals[0]}"


def _pack_scalar_into(raw, offset, fmt, value):
    raw[offset:offset + FMT_SIZE[fmt]] = _pack_scalar(fmt, value)


# ── 显示 ─────────────────────────────────────────────────
def print_dump(drv):
    cfg = drv.read_param()
    d = parse_config(cfg)

    print(f"config_t {CONFIG_SIZE}B 解析：")
    print("\n-- 受保护区 (offset 0~10, 只读) --")
    h = d["_header"]
    print(f"  magic=0x{h['magic']:08X}  硬件版本=v{h['hw_ver'][0]}.{h['hw_ver'][1]}.{h['hw_ver'][2]}  "
          f"固件版本=v{h['sw_ver'][0]}.{h['sw_ver'][1]}  reserved={h['reserved']}  crc=0x{h['crc']:02X}")

    print("\n-- 可读字段 --")
    for name, offset, fmt, count, desc in FIELD_TABLE:
        val = d[name]
        if name in CTRL_PARAMS:
            print(f"  {name:<20} @{offset:>3} {fmt:<3}×{count:<2} {desc}")
            for i in range(4):
                kp, ki, kd, ilim = val[i * 4:i * 4 + 4]
                print(f"      CH{i + 1}: kp={kp:g}  ki={ki:g}  kd={kd:g}  ilim={ilim:g}")
        elif count > 1:
            print(f"  {name:<20} @{offset:>3} {fmt:<3}×{count:<2} {desc}")
            print(f"      CH1~4: {val}")
        else:
            print(f"  {name:<20} @{offset:>3} {fmt:<3}    {desc}")
            print(f"      = {val}")

    dec = d["_decoded"]
    print("\n-- 位域按通道解码 --")
    print(f"  模式:      " + "  ".join(f"CH{i + 1}={dec['mode_ch'][i]}" for i in range(4)))
    print(f"  电机反转:  " + "  ".join(f"CH{i + 1}=0x{dec['motor_inv_ch'][i]:X}" for i in range(4)))
    print(f"  速度环类型:" + "  ".join(f"CH{i + 1}={dec['speed_pid_type_ch'][i]}" for i in range(4)))
    print(f"  位置环类型:" + "  ".join(f"CH{i + 1}={dec['pos_pid_type_ch'][i]}" for i in range(4)))
    print(f"  滤波类型:  " + "  ".join(f"CH{i + 1}={dec['filter_type_ch'][i]}" for i in range(4)))
    print(f"  遥控通道:  " + "  ".join(f"CH{i + 1}=CH{dec['sbus_ch_ch'][i] + 1}" for i in range(4)))
    print(f"  方向通道:  " + "  ".join(f"CH{i + 1}=CH{dec['rc_dir_ch_ch'][i] + 1}" for i in range(4)))
    print(f"  映射模式:  " + "  ".join(f"CH{i + 1}={dec['rc_map_mode_ch'][i]}" for i in range(4)))
    print(f"  方向使能:  " + "  ".join(f"CH{i + 1}={dec['rc_dir_en_ch'][i]}" for i in range(4)))
    proto = {0: "未识别", 1: "SBUS", 2: "UART", 3: "ELRS"}
    print(f"  comm_flags: protocol={proto.get(dec['comm_protocol'], dec['comm_protocol'])}  "
          f"sbus_inv={dec['comm_sbus_inv']}  ctrl_priority={dec['comm_priority']}")


def cmd_set(drv, field_spec, values):
    """set：读当前配置 → 改单字段 → WRITE_PARAM 写回（仅 RAM）。"""
    buf = bytearray(drv.read_param())
    note = apply_set(buf, field_spec, values)
    drv.write_param(bytes(buf))
    print(f"{note}  → 已写入 (RAM only)；执行 `save` 持久化到 EEPROM。")


def cmd_field(drv, target, value):
    """field：WRITE_FIELD 按偏移写单个字段（演示按类型打包）。"""
    # 解析目标：数字偏移（支持 0x 前缀 / 前导零）或 字段名
    name = None
    offset = None
    try:
        offset = int(target, 0)
    except ValueError:
        if target.strip().isdigit():
            offset = int(target)            # 如 "08" 按十进制偏移处理
        else:
            name = ALIASES.get(target, target)
            if name not in FIELD_BY_NAME:
                raise KeyError(f"未知字段: {target}（可用: {', '.join(FIELD_BY_NAME)}）")
    if name is not None:
        field = FIELD_BY_NAME[name]
        offset = field[1]
    else:
        field = FIELD_BY_OFFSET.get(offset)
        if field is None:
            raise ValueError(f"偏移 {offset} 不在字段表内（可用偏移: "
                             f"{sorted(FIELD_BY_OFFSET)}）")
    _, off, fmt, count, desc = field

    if offset < 12:
        raise ValueError(f"offset {off} 位于受保护区（规范：WRITE_FIELD 拒绝 offset<12）")
    if count > 1:
        raise ValueError(f"{field[0]} 是数组字段（{count} 个值），请用 `set` 命令修改")

    v = parse_value(value, fmt)
    packed = _pack_scalar(fmt, v)
    err = drv.write_field(offset, packed)
    if err is None:
        raise RuntimeError("WRITE_FIELD 超时")
    if err != ACK_OK:
        raise RuntimeError(f"WRITE_FIELD 失败 (err=0x{err:02X})")
    print(f"WRITE_FIELD offset={off} ({field[0]}, {fmt}) = {v}  → OK (RAM only)")


def cmd_save(drv):
    err = drv.save()
    if err is None:
        raise RuntimeError("SAVE_EEPROM 超时")
    if err != ACK_OK:
        raise RuntimeError(f"SAVE_EEPROM 失败 (err=0x{err:02X})")
    print("SAVE_EEPROM OK —— 当前 RAM 配置已写入 EEPROM。")


# ── CLI ──────────────────────────────────────────────────
def pick_port(explicit):
    if explicit:
        return explicit
    from serial.tools import list_ports
    cands = [p for p in list_ports.comports()
             if p.vid == 0x1A86 or "CH340" in (p.description or "").upper()]
    if not cands:
        return None
    print(f"自动选择串口: {cands[0].device}")
    return cands[0].device


def interactive(drv):
    """交互模式：菜单选择 dump / set / save / exit。"""
    print("交互模式：1=dump  2=set  3=save  4=退出（q 亦可）")
    while True:
        try:
            line = input("\nconfig> ").strip()
        except EOFError:
            print()
            return
        if line in ("4", "q", "quit", "exit"):
            return
        if line in ("1", "dump"):
            print_dump(drv)
        elif line in ("3", "save"):
            cmd_save(drv)
        elif line in ("2", "set"):
            try:
                spec = input("  字段[:通道] 值... (如 speed_olim:1 800, mode:1 speed): ").strip()
                parts = spec.split()
                if len(parts) < 2:
                    print("  格式: <字段[:通道]> <值> [值...]")
                    continue
                cmd_set(drv, parts[0], parts[1:])
            except (ValueError, KeyError, RuntimeError) as e:
                print(f"  [错误] {e}")
        else:
            print("  输入 1~4 选择操作")


def main():
    setup_console()
    parser = argparse.ArgumentParser(description="Motor Driver Controller 配置读写工具")
    parser.add_argument("--port", default=None, help="串口号；缺省自动选择第一个 CH340")
    sub = parser.add_subparsers(dest="cmd")

    p_dump = sub.add_parser("dump", help="读取并打印全部配置")
    p_set = sub.add_parser("set", help="修改单字段并 WRITE_PARAM 写回")
    p_set.add_argument("field", help="字段名[ :通道1~4]，如 speed_olim:1")
    p_set.add_argument("values", nargs="+", help="值（数组字段可多个）")
    p_field = sub.add_parser("field", help="WRITE_FIELD 按偏移写单个字段")
    p_field.add_argument("target", help="字段名 或 数字偏移")
    p_field.add_argument("value", help="值")
    sub.add_parser("save", help="SAVE_EEPROM 持久化")
    args = parser.parse_args()

    port = pick_port(args.port)
    if port is None:
        print("未找到 CH340 串口，请用 --port 手动指定。")
        return

    try:
        drv = Driver(port)
    except serial.SerialException as e:
        print(f"[错误] 串口打开失败: {e}")
        return

    try:
        if args.cmd == "dump":
            print_dump(drv)
        elif args.cmd == "set":
            cmd_set(drv, args.field, args.values)
        elif args.cmd == "field":
            cmd_field(drv, args.target, args.value)
        elif args.cmd == "save":
            cmd_save(drv)
        else:
            interactive(drv)
    except (ValueError, KeyError, RuntimeError) as e:
        print(f"[错误] {e}")
    finally:
        drv.close()


if __name__ == "__main__":
    sys.exit(main())
