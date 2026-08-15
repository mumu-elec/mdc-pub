#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
06_config_manager — 配置读写工具（config_t 231B）
==================================================
基于 mdc_lib 的配置读写工具，演示完整配置读写流程：

    dump                        读取全部配置并打印
                                （md_bin_read_param 发送 → 接收 231B →
                                 md_parse_config 解析为 dict 打印）
    set <field[:ch]> <值...>    修改单字段（读 → 改 dict → 写回）
                                （md_pack_config 打包 + md_bin_write_param 写回）
    field <名称|偏移> <值>      演示 md_bin_write_field 按偏移写单个标量字段
    save                        演示 md_bin_save() 持久化到 EEPROM
    （无子命令）                 交互模式：菜单选择 dump / set / save

字段名与 mdc_lib.md_parse_config 返回的 dict 键名一一对应（见 mdc_lib/API.md §6.5）：
    * 标量：baud_rate / cmd_timeout_ms / protocol / sbus_inv / ctrl_priority /
            sbus_range_min / sbus_range_max
    * 通道数组（4 元素）：control_mode / motor_invert / encoder_cpr /
            speed_period_ms / speed_pid_type / speed_olim / speed_kp / speed_ki /
            speed_kd / speed_ilim / pos_period_ms / pos_pid_type / pos_kp / pos_ki /
            pos_kd / pos_ilim / pos_olim / pos_angle_cpr / speed_filter_type /
            speed_filter_window / sbus_channel / rc_dir_ch / rc_map_mode /
            rc_dir_en / sbus_param
通道字段语法：`字段名:通道1~4`，例如 `speed_olim:1 800`、`mode:2 speed`。

用法：
    python config_manager.py dump
    python config_manager.py --port COM5 set encoder_cpr:1 500
    python config_manager.py set mode:1 speed
    python config_manager.py set speed_pid:1 0.5 0.02 0.01 0.5
    python config_manager.py field cmd_timeout_ms 500
    python config_manager.py save
    python config_manager.py
"""

import argparse
import time

import serial

# mdc_lib 已随例程内置（本目录 mdc_lib.py）；如需更新库版本，用 ../mdc_lib/python/mdc_lib.py 覆盖
import os, sys
import mdc_lib

BAUDRATE = 2000000

MODE_NAME = {0: "open(开环)", 1: "speed(速度)", 2: "pos(位置)"}


def setup_console():
    """控制台输出统一 UTF-8（兼容 GBK / 输出重定向场景）。"""
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass

# ── 字段表（键名 = mdc_lib.md_parse_config 返回的 dict 键名）───────────
# (name, fmt, count, offset, desc)
# offset 为 config_t 字节偏移（协议规范 §5），仅 field 子命令（md_bin_write_field）使用；
# 数组字段的 offset 为协议规范 §5 中该字段的起始偏移（供参考）。
FIELD_TABLE = [
    ("baud_rate",           "u32", 1, 11,  "USART2 波特率"),
    ("cmd_timeout_ms",      "u16", 1, 15,  "指令超时保护 (ms, 0=关闭)"),
    ("protocol",            "u8",  1, 17,  "comm_flags bit0-3: 1=SBUS 2=UART 3=ELRS"),
    ("sbus_inv",            "u8",  1, 17,  "comm_flags bit4: SBUS 反相"),
    ("ctrl_priority",       "u8",  1, 17,  "comm_flags bit5: 0=USART2 优先 1=USB 优先"),
    ("control_mode",        "u8",  4, 18,  "每电机2bit: 0=开环 1=速度 2=位置"),
    ("motor_invert",        "u8",  4, 19,  "每电机2bit: bit0=引脚反转 bit1=编码器极性"),
    ("encoder_cpr",         "u16", 4, 20,  "编码器线数"),
    ("speed_period_ms",     "u16", 4, 28,  "速度环周期 (ms)"),
    ("speed_pid_type",      "u8",  4, 36,  "每电机4bit: 0=位置式 1=增量式"),
    ("speed_olim",          "u16", 4, 38,  "速度环输出限幅 (PWM 0~1000)"),
    ("speed_kp",            "f32", 4, 46,  "速度环 Kp"),
    ("speed_ki",            "f32", 4, 50,  "速度环 Ki"),
    ("speed_kd",            "f32", 4, 54,  "速度环 Kd"),
    ("speed_ilim",          "f32", 4, 58,  "速度环积分限幅"),
    ("pos_period_ms",       "u16", 4, 110, "位置环周期 (ms)"),
    ("pos_pid_type",        "u8",  4, 118, "每电机4bit: 0=位置式 1=增量式"),
    ("pos_kp",              "f32", 4, 120, "位置环 Kp"),
    ("pos_ki",              "f32", 4, 124, "位置环 Ki"),
    ("pos_kd",              "f32", 4, 128, "位置环 Kd"),
    ("pos_ilim",            "f32", 4, 132, "位置环积分限幅"),
    ("pos_olim",            "f32", 4, 184, "位置环输出限幅 (RPM)"),
    ("pos_angle_cpr",       "u16", 4, 200, "位置环转一圈脉冲数 (0=用 encoder_cpr)"),
    ("speed_filter_type",   "u8",  4, 208, "每电机4bit: 0=无 1=滑动平均 2=低通 3=中值"),
    ("speed_filter_window", "u8",  4, 210, "滤波窗口 (MA/中值 1~32, LPF 1~99)"),
    ("sbus_channel",        "u8",  4, 214, "遥控通道映射 (API 值 1~16)"),
    ("rc_dir_ch",           "u8",  4, 216, "方向映射通道 (API 值 1~16)"),
    ("rc_map_mode",         "u8",  4, 218, "每电机1bit: 0=中心零点 1=min零点"),
    ("rc_dir_en",           "u8",  4, 218, "每电机1bit: 方向映射使能"),
    ("sbus_param",          "u16", 4, 219, "遥控行程"),
    ("sbus_range_min",      "u16", 1, 227, "通道值下边界"),
    ("sbus_range_max",      "u16", 1, 229, "通道值上边界"),
]
FIELD_BY_NAME = {f[0]: f for f in FIELD_TABLE}

# set 命令别名（兼容常见叫法）
ALIASES = {"mode": "control_mode", "cpr": "encoder_cpr",
           "speed_ctrl_params": "speed_pid", "pos_ctrl_params": "pos_pid"}

# PID 组：一次设置某通道的 kp/ki/kd/ilim（对应 mdc_lib dict 的四个字段）
PID_GROUPS = {
    "speed_pid": ("speed_kp", "speed_ki", "speed_kd", "speed_ilim"),
    "pos_pid":   ("pos_kp", "pos_ki", "pos_kd", "pos_ilim"),
}

# field 子命令支持的单字段（标量、dict 值 ↔ 原始字节一一对应）：
# 名称 → (偏移, 字节数)；位域/数组字段请用 set + WRITE_PARAM（mdc_lib 自动打包）
FIELD_OFFSETS = {"baud_rate": (11, 4), "cmd_timeout_ms": (15, 2),
                 "sbus_range_min": (227, 2), "sbus_range_max": (229, 2)}
FIELD_BY_OFFSET = {v[0]: k for k, v in FIELD_OFFSETS.items()}

# 显示分组
SECTIONS = [
    ("电机×4",   ["control_mode", "motor_invert", "encoder_cpr"]),
    ("速度环",   ["speed_period_ms", "speed_pid_type", "speed_olim",
                  "speed_kp", "speed_ki", "speed_kd", "speed_ilim"]),
    ("位置环",   ["pos_period_ms", "pos_pid_type",
                  "pos_kp", "pos_ki", "pos_kd", "pos_ilim",
                  "pos_olim", "pos_angle_cpr"]),
    ("滤波/遥控", ["speed_filter_type", "speed_filter_window",
                   "sbus_channel", "rc_dir_ch", "rc_map_mode", "rc_dir_en",
                   "sbus_param", "sbus_range_min", "sbus_range_max"]),
]


# ── 串口收发层（打包/解析全部调用 mdc_lib）────────────────
class Driver:
    """串口收发封装：发送 mdc_lib 打包好的帧，用 MDParser 流式解析应答。"""

    def __init__(self, port):
        self.ser = serial.Serial(port, BAUDRATE, timeout=0.05)
        self.ser.reset_input_buffer()
        self.parser = mdc_lib.MDParser()

    def _wait_frame(self, want_cmd, want_len=None, timeout=1.0):
        """读取帧直到匹配 want_cmd（可选 want_len），返回 payload；超时返回 None。"""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            chunk = self.ser.read(self.ser.in_waiting or 1)
            for b in chunk:
                r = self.parser.feed(b)
                if r is None:
                    continue
                c, d = r
                if c == want_cmd and (want_len is None or len(d) == want_len):
                    return d
        return None

    def _ack(self, cmd, timeout=0.5):
        data = self._wait_frame(cmd, 1, timeout)
        if data is None:
            return None
        return mdc_lib.md_parse_ack(data).err

    def read_param(self):
        """0x10 READ_PARAM：发送 → 接收 231B config_t 应答。"""
        self.ser.write(mdc_lib.md_bin_read_param())
        data = self._wait_frame(mdc_lib.MD_CMD_READ_PARAM,
                                mdc_lib.MD_CONFIG_SIZE, timeout=2.0)
        if data is None:
            raise RuntimeError("READ_PARAM 超时/数据长度不符")
        return data

    def write_param(self, cfg):
        """0x11 WRITE_PARAM：cfg 为 dict（自动 md_pack_config）或 231B bytes。"""
        self.ser.write(mdc_lib.md_bin_write_param(cfg))
        return self._ack(mdc_lib.MD_CMD_WRITE_PARAM)

    def write_field(self, field_id, value, value_len):
        """0x12 WRITE_FIELD：按偏移写单个标量字段。"""
        self.ser.write(mdc_lib.md_bin_write_field(field_id, value, value_len))
        return self._ack(mdc_lib.MD_CMD_WRITE_FIELD)

    def save(self):
        """0x20 SAVE_EEPROM：RAM 配置写入 EEPROM（固件耗时约 190ms）。"""
        self.ser.write(mdc_lib.md_bin_save())
        return self._ack(mdc_lib.MD_CMD_SAVE_EEPROM, timeout=1.0)

    def close(self):
        if self.ser.is_open:
            self.ser.close()


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


def apply_set(cfg, field_spec, values):
    """在 mdc_lib.md_parse_config 返回的 dict 上应用 set 修改，返回说明字符串。

    values 为字符串列表；范围/类型校验由 md_pack_config 完成（越界抛 ValueError）。
    """
    if ":" in field_spec:
        name, ch_txt = field_spec.rsplit(":", 1)
        ch = int(ch_txt) - 1
        if not 0 <= ch <= 3:
            raise ValueError("通道取值 1~4")
    else:
        name, ch = field_spec, None

    name = ALIASES.get(name, name)

    # PID 组：speed_pid / pos_pid → kp/ki/kd/ilim 四个 dict 字段
    if name in PID_GROUPS:
        keys = PID_GROUPS[name]
        vals = [parse_value(v, "f32") for v in values]
        if ch is not None:
            if len(vals) != 4:
                raise ValueError(f"{name}:ch 需要 4 个值 (kp ki kd ilim)")
            for key, v in zip(keys, vals):
                cfg[key][ch] = v
            return f"{name} 通道{ch + 1} kp/ki/kd/ilim = {vals}"
        if len(vals) != 16:
            raise ValueError(f"{name}（不带通道）需 16 个值 (kp ki kd ilim × 4 通道)")
        for key, j in zip(keys, range(4)):
            cfg[key] = list(vals[j::4])        # 每 4 个一组 = 4 个通道
        return f"{name} 四通道 kp/ki/kd/ilim = {vals}"

    if name == "control_mode":
        vals = [parse_mode_word(v) for v in values]
        if ch is not None:
            if len(vals) != 1:
                raise ValueError("control_mode:ch 只需 1 个值")
            cfg["control_mode"][ch] = vals[0]
            return f"control_mode 通道{ch + 1} = {MODE_NAME.get(vals[0], vals[0])}"
        if len(vals) != 4:
            raise ValueError("control_mode（不带通道）需 4 个值，对应 4 通道")
        cfg["control_mode"] = vals
        return ("control_mode 四通道 = "
                + ", ".join(MODE_NAME.get(v, str(v)) for v in vals))

    if name not in FIELD_BY_NAME:
        raise KeyError(f"未知字段: {name}（可用: {', '.join(FIELD_BY_NAME)}）")
    _, fmt, count, _, _ = FIELD_BY_NAME[name]
    vals = [parse_value(v, fmt) for v in values]

    if count > 1:                              # 通道数组字段
        if ch is not None:
            if len(vals) != 1:
                raise ValueError(f"{name}:ch 只需 1 个值")
            cfg[name][ch] = vals[0]
            return f"{name} 通道{ch + 1} = {vals[0]}"
        if len(vals) != count:
            raise ValueError(f"{name} 需要 {count} 个值，实际 {len(vals)}")
        cfg[name] = vals
        return f"{name} = {vals}"

    if len(vals) != 1:                         # 标量字段
        raise ValueError(f"{name} 只需 1 个值")
    cfg[name] = vals[0]
    return f"{name} = {vals[0]}"


# ── 显示 ─────────────────────────────────────────────────
def _fmt_value(name, fmt, val):
    if fmt == "f32":
        return "  ".join(f"CH{i + 1}={v:g}" for i, v in enumerate(val))
    return "  ".join(f"CH{i + 1}={v}" for i, v in enumerate(val))


def print_dump(drv):
    raw = drv.read_param()
    cfg = mdc_lib.md_parse_config(raw)         # 231B → dict（键名见 mdc_lib/API.md §6.5）

    print(f"config_t {len(raw)}B（{mdc_lib.MD_CONFIG_SIZE}B）解析（mdc_lib.md_parse_config）：")
    print(f"  受保护区 offset 0~10 (magic/硬件版本/固件版本/crc) 为只读区，未在解析结果中；"
          f"前 16 字节: {raw[:16].hex(' ')}")
    print(f"  版本号可查询文本指令 /version（md_text_version）")

    print("\n-- 通讯 --")
    print(f"  baud_rate={cfg['baud_rate']}   cmd_timeout_ms={cfg['cmd_timeout_ms']}")
    print(f"  protocol={cfg['protocol']} (1=SBUS 2=UART 3=ELRS)  "
          f"sbus_inv={cfg['sbus_inv']}  ctrl_priority={cfg['ctrl_priority']}")

    for title, names in SECTIONS:
        print(f"\n-- {title} --")
        for name in names:
            _, fmt, count, offset, desc = FIELD_BY_NAME[name]
            val = cfg[name]
            if count == 1:
                print(f"  {name:<20} = {val}    {desc}")
            else:
                print(f"  {name:<20} {_fmt_value(name, fmt, val)}")
                print(f"  {'':<20} ({desc})")
    print("\n提示: set 修改后需执行 save 才持久化到 EEPROM。")


# ── 子命令 ───────────────────────────────────────────────
def cmd_set(drv, field_spec, values):
    """set：读当前配置 → 改 dict → md_pack_config + md_bin_write_param 写回（仅 RAM）。"""
    cfg = mdc_lib.md_parse_config(drv.read_param())
    note = apply_set(cfg, field_spec, values)
    err = drv.write_param(cfg)                 # dict → mdc_lib 自动 md_pack_config 打包
    if err is None:
        raise RuntimeError("WRITE_PARAM 超时")
    if err != mdc_lib.MD_ERR_OK:
        raise RuntimeError(f"WRITE_PARAM 失败 (err=0x{err:02X})")
    print(f"{note}  → 已写入 (RAM only)；执行 `save` 持久化到 EEPROM。")


def cmd_field(drv, target, value):
    """field：演示 md_bin_write_field 按偏移写单个标量字段（支持字段名或数字偏移）。"""
    try:
        offset = int(target, 0)
    except ValueError:
        if target.strip().isdigit():
            offset = int(target)               # 如 "08" 按十进制偏移处理
        else:
            name = ALIASES.get(target, target)
            if name not in FIELD_OFFSETS:
                raise KeyError(f"未知字段: {target}（field 支持: {', '.join(FIELD_OFFSETS)}，"
                               f"或数字偏移 {sorted(FIELD_BY_OFFSET)}）")
            offset = FIELD_OFFSETS[name][0]
    else:
        name = FIELD_BY_OFFSET.get(offset)
        if name is None:
            raise ValueError(f"偏移 {offset} 不支持 field 命令（可用偏移: "
                             f"{sorted(FIELD_BY_OFFSET)}）")

    _, fmt, _, _, desc = FIELD_BY_NAME[name]
    v = parse_value(value, fmt)
    err = drv.write_field(offset, v, FIELD_OFFSETS[name][1])
    if err is None:
        raise RuntimeError("WRITE_FIELD 超时")
    if err != mdc_lib.MD_ERR_OK:
        raise RuntimeError(f"WRITE_FIELD 失败 (err=0x{err:02X})")
    print(f"md_bin_write_field(field_id={offset}, {name}, {v}) → OK (RAM only)")


def cmd_save(drv):
    err = drv.save()
    if err is None:
        raise RuntimeError("SAVE_EEPROM 超时")
    if err != mdc_lib.MD_ERR_OK:
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
                spec = input("  字段[:通道] 值... (如 speed_olim:1 800, mode:1 speed, "
                             "speed_pid:1 0.5 0.02 0.01 0.5): ").strip()
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

    sub.add_parser("dump", help="读取并打印全部配置")
    p_set = sub.add_parser("set", help="修改单字段并 WRITE_PARAM 写回")
    p_set.add_argument("field", help="字段名[:通道1~4]，如 speed_olim:1")
    p_set.add_argument("values", nargs="+", help="值（数组字段可多个）")
    p_field = sub.add_parser("field", help="演示 md_bin_write_field 按偏移写单个标量字段")
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
