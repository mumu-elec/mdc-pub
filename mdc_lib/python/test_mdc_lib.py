# -*- coding: utf-8 -*-
"""
test_mdc_lib.py — mdc_lib 自测脚本（无硬件，直接断言）
=====================================================

覆盖 API.md §8 一致性验证要求：
    * crc8([0x01,0x00]) == 0x15 / crc8(b"123456789") == 0xF4
    * build_frame(0x01, b"") == AA 01 00 15
    * build_frame(0x40, [0x32,0x00])（SUBSCRIBE 50ms）== AA 40 02 32 00 <crc>
    * motor_ctrl(100,-200,0,300) DATA == 64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00
    * parse_status 56B / 72B 字段与 §6.2 偏移一致（逐项断言）
    * parse_config ↔ pack_config 往返无损（位域、float、数组全部一致）
    * 流式解析器：垃圾字节 + 坏 CRC 帧 + 正常帧 → 只输出正常帧

另覆盖：全部文本指令、15 个二进制命令的帧布局、md_parse_ack/detect/sbus、
MDC 便捷类、参数非法抛 ValueError 的路径。

运行：python test_mdc_lib.py    （全部断言通过则退出码 0）
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# 控制台输出统一 UTF-8（兼容 GBK 控制台 / 输出重定向场景）
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

import mdc_lib  # noqa: E402
from mdc_lib import (  # noqa: E402
    MDC, MDParser, md_bin_debug_sbus, md_bin_debug_speed, md_bin_enter_bl,
    md_bin_factory_reset, md_bin_load, md_bin_motor_ctrl, md_bin_motor_raw,
    md_bin_ping, md_bin_read_param, md_bin_reboot, md_bin_save,
    md_bin_subscribe, md_bin_unsubscribe, md_bin_write_field,
    md_bin_write_param, md_build_frame, md_crc8, md_pack_config,
    md_parse_ack, md_parse_config, md_parse_detect, md_parse_frame,
    md_parse_sbus, md_parse_status, md_text_build, md_text_check,
    md_text_detect, md_text_enczero, md_text_help, md_text_load,
    md_text_mode, md_text_reset, md_text_save, md_text_status, md_text_version,
)

# ---------------------------------------------------------------------------
# 极简断言框架：统计 PASS / FAIL，全部通过退出码 0
# ---------------------------------------------------------------------------
PASS = 0
FAIL = 0
FAILURES = []


def check(cond, msg):
    """单条断言：cond 为真记 PASS，否则记 FAIL 并打印信息。"""
    global PASS, FAIL
    if cond:
        PASS += 1
    else:
        FAIL += 1
        FAILURES.append(msg)
        print("FAIL: " + msg)


def check_eq(actual, expected, msg):
    """断言 actual == expected，失败时打印期望/实际值。"""
    check(actual == expected,
          "%s\n    期望: %r\n    实际: %r" % (msg, expected, actual))


def check_raises(exc, fn, *args, **kw):
    """断言调用 fn(*args, **kw) 抛出 exc 类型异常。"""
    try:
        fn(*args, **kw)
    except exc:
        check(True, "应抛 %s: %s%r" % (exc.__name__, fn.__name__, args))
    except Exception as e:  # noqa: BLE001
        check(False, "应抛 %s 但抛了 %s: %s%r" % (exc.__name__, type(e).__name__, fn.__name__, args))
    else:
        check(False, "应抛 %s 但未抛: %s%r" % (exc.__name__, fn.__name__, args))


# ---------------------------------------------------------------------------
# §1 CRC8（API.md §8 向量）
# ---------------------------------------------------------------------------
print("== §1 CRC8 ==")
check_eq(md_crc8([0x01, 0x00]), 0x15, "crc8([0x01,0x00]) == 0x15")
check_eq(md_crc8(b"123456789"), 0xF4, "crc8(b'123456789') == 0xF4")
check_eq(md_crc8(bytearray([0x01, 0x00])), 0x15, "crc8(bytearray) 同样可用")
check_eq(md_crc8(b""), 0x00, "crc8(b'') == 0（初值 0）")
check_raises(ValueError, md_crc8, [0x01, 0x100])   # 元素越界
check_raises(ValueError, md_crc8, [0x01, -1])

# ---------------------------------------------------------------------------
# §2 组帧（API.md §8 向量）
# ---------------------------------------------------------------------------
print("== §2 md_build_frame ==")
check_eq(md_build_frame(0x01, b""), b"\xAA\x01\x00\x15", "build_frame(0x01, b'') == AA 01 00 15")
sub = md_build_frame(0x40, b"\x32\x00")
check_eq(sub, b"\xAA\x40\x02\x32\x00\x9E", "build_frame(0x40, [0x32,0x00]) == AA 40 02 32 00 9E")
check_eq(md_build_frame(0x40, [0x32, 0x00]), sub, "data 接受整数列表")
big = bytes(range(250))
check_eq(len(md_build_frame(0x11, big)), 4 + 250, "DATA 250B 上限可打包")
check_raises(ValueError, md_build_frame, 0x11, bytes(251))   # 超长
check_raises(ValueError, md_build_frame, 0x100)              # cmd 越界
check_raises(ValueError, md_build_frame, -1)
check_raises(ValueError, md_build_frame, 0x01, "not-bytes")  # 类型非法

# ---------------------------------------------------------------------------
# §3 md_parse_frame 帧级解析
# ---------------------------------------------------------------------------
print("== §3 md_parse_frame ==")
fr = md_parse_frame(sub)
check(fr.valid and fr.cmd == 0x40 and fr.payload == b"\x32\x00",
      "parse_frame 有效帧 → cmd/payload 正确")
bad = bytearray(sub)
bad[-1] ^= 0xFF                                       # 破坏 CRC
check(md_parse_frame(bytes(bad)).valid is False, "坏 CRC → valid=False")
check(md_parse_frame(b"\x00\x01\x00\x15").valid is False, "坏 SYNC → valid=False")
check(md_parse_frame(b"\xAA\x01").valid is False, "长度不足 → valid=False")
check(md_parse_frame(sub + b"\x00").valid is False, "长度不符（多 1 字节）→ valid=False")
check_raises(ValueError, md_parse_frame, "not-bytes")

# ---------------------------------------------------------------------------
# §4 MDParser 流式解析器
# ---------------------------------------------------------------------------
print("== §4 MDParser ==")


def feed_all(parser, data):
    """逐字节喂入，返回全部 (cmd, payload) 输出。"""
    out = []
    for b in data:
        r = parser.feed(b)
        if r is not None:
            out.append(r)
    return out


# 4.1 单帧逐字节
p = MDParser()
out = feed_all(p, md_bin_ping())
check_eq(out, [(0x01, b"")], "单帧逐字节喂入 → (cmd, payload)")

# 4.2 垃圾字节 + 坏 CRC 帧 + 两帧连发 → 只输出正常帧（API.md §8 向量）
frame_a = md_bin_motor_raw(0, 0, 1000)            # 0x30
status_56 = struct.pack("<4i4f4iII", 1, 2, 3, 4, 0.0, 0.0, 0.0, 0.0,
                        10, 20, 30, 40, 100, 200)
frame_b = md_build_frame(0xF0, status_56)         # 0xF0 STATUS_REPORT 56B
bad_frame = bytearray(md_build_frame(0x10, b"\x01\x02\x03"))
bad_frame[-1] ^= 0x55                              # 坏 CRC
stream = (b"noise bytes \x00\xFF"                 # 无 0xAA 的文本噪声
          + b"\xAA\xFF\x00"                       # LEN=255 > 250：伪同步应被丢弃
          + bytes(bad_frame)                       # 坏 CRC 帧
          + frame_a + frame_b)                     # 两帧连发
p2 = MDParser()
out2 = feed_all(p2, stream)
check_eq(out2, [(0x30, b"\x00\x00\xe8\x03"), (0xF0, status_56)],
         "垃圾/坏CRC/两帧连发 → 只输出两帧正常帧（顺序正确）")

# 4.3 DATA 内含 0xAA：不得被误当作新帧同步
inner = struct.pack("<4i", 0xAAAA, -1, 0x55AA, 7)
frame_inner = md_build_frame(0x31, inner)
p3 = MDParser()
out3 = feed_all(p3, b"junk" + frame_inner + md_bin_ping())
check_eq(out3, [(0x31, inner), (0x01, b"")], "DATA 内含 0xAA 不被误同步")

# 4.4 分多次喂（跨调用收帧）
p4 = MDParser()
seg = frame_a + frame_b
got4 = []
for i in range(0, len(seg), 7):                   # 每 7 字节喂一次
    for b in seg[i:i + 7]:
        r = p4.feed(b)
        if r is not None:
            got4.append(r)
check_eq(got4, [(0x30, b"\x00\x00\xe8\x03"), (0xF0, status_56)],
         "分片喂入跨调用收帧")

# 4.5 坏 CRC 后继续扫描后续正常帧
p5 = MDParser()
out5 = feed_all(p5, bytes(bad_frame) + frame_a)
check_eq(out5, [(0x30, b"\x00\x00\xe8\x03")], "坏 CRC 帧丢弃后继续收后续帧")

# 4.6 非法输入 / 参数
check_raises(ValueError, MDParser, 0)             # max_data 非法
check_raises(ValueError, MDParser, 251)
check_raises(ValueError, MDParser().feed, -1)
check_raises(ValueError, MDParser().feed, 256)
check_raises(ValueError, MDParser().feed, "a")

# 4.7 小 max_data：超过上限的帧被丢弃，合法小帧正常
p6 = MDParser(max_data=4)
small = md_build_frame(0x30, b"\x00\x00\xe8\x03")   # plen=4 ≤ max_data
too_big = md_build_frame(0x30, b"\x01\x02\x03\x04\x05")  # plen=5 > max_data
out6 = feed_all(p6, too_big + small)
check_eq(out6, [(0x30, b"\x00\x00\xe8\x03")], "max_data=4 时 plen=5 帧被丢弃，plen=4 帧正常")

# 4.8 末尾残留噪声不产生假帧
p7 = MDParser()
out7 = feed_all(p7, frame_a + b"\xAA\x05\xff\x00")
check_eq(out7, [(0x30, b"\x00\x00\xe8\x03")], "残留不完整帧不输出")

# ---------------------------------------------------------------------------
# §5 文本指令层
# ---------------------------------------------------------------------------
print("== §5 文本指令 ==")
check_eq(md_text_build("/mode", "1 speed"), b"/mode 1 speed\n", "text_build 带参数")
check_eq(md_text_build("/mode", None), b"/mode\n", "text_build args=None 省略")
check_eq(md_text_build("mode", "1 speed"), b"/mode 1 speed\n", "cmd 自动补 '/'")
check_eq(md_text_build("/speedctrl", "1 0.5 0.02 0.01 500 800 10 0"),
         b"/speedctrl 1 0.5 0.02 0.01 500 800 10 0\n", "speedctrl 示例")
check_eq(md_text_build("/uart2", "115200 0 uart"), b"/uart2 115200 0 uart\n", "uart2 示例")
check_eq(md_text_build("/sbusrange", "172 1811"), b"/sbusrange 172 1811\n", "sbusrange 示例")
check_eq(md_text_version(), b"/version\n", "/version")
check_eq(md_text_help(), b"/help\n", "/help")
check_eq(md_text_status(), b"/status\n", "/status")
check_eq(md_text_check(), b"/check\n", "/check")
check_eq(md_text_detect(), b"/detect\n", "/detect")
check_eq(md_text_save(), b"/save\n", "/save")
check_eq(md_text_load(), b"/load\n", "/load")
check_eq(md_text_reset(), b"/reset\n", "/reset")
check_eq(md_text_enczero(1), b"/enczero 1\n", "/enczero 1")
check_eq(md_text_enczero(4), b"/enczero 4\n", "/enczero 4")
check_eq(md_text_mode(1), b"/mode 1\n", "/mode 1（读取模式）")
check_eq(md_text_mode(1, "speed"), b"/mode 1 speed\n", "/mode 1 speed")
check_eq(md_text_mode(2, "pos"), b"/mode 2 pos\n", "/mode 2 pos")
check_raises(ValueError, md_text_enczero, 0)
check_raises(ValueError, md_text_enczero, 5)
check_raises(ValueError, md_text_mode, 0)
check_raises(ValueError, md_text_mode, 5, "speed")
check_raises(ValueError, md_text_build, "")

# ---------------------------------------------------------------------------
# §6 二进制命令层（15 个打包函数）
# ---------------------------------------------------------------------------
print("== §6 二进制命令 ==")
check_eq(md_bin_ping(), b"\xAA\x01\x00\x15", "ping() == AA 01 00 15")
check_eq(md_bin_read_param(), md_build_frame(0x10), "read_param 帧")
check_eq(md_bin_save(), md_build_frame(0x20), "save 帧")
check_eq(md_bin_load(), md_build_frame(0x21), "load 帧")
check_eq(md_bin_factory_reset(), md_build_frame(0x22), "factory_reset 帧")
check_eq(md_bin_unsubscribe(), md_build_frame(0x41), "unsubscribe 帧")
check_eq(md_bin_enter_bl(), md_build_frame(0x52), "enter_bl 帧")
check_eq(md_bin_reboot(), md_build_frame(0x53), "reboot 帧")

# write_param：bytes 与 dict 两种形态
raw231 = bytes(range(231))
check_eq(md_bin_write_param(raw231), md_build_frame(0x11, raw231), "write_param(bytes)")
cfg_demo = {"baud_rate": 115200, "protocol": 2}
check_eq(md_bin_write_param(cfg_demo), md_build_frame(0x11, md_pack_config(cfg_demo)),
         "write_param(dict) 自动打包")
check_raises(ValueError, md_bin_write_param, bytes(230))   # 长度不符

# write_field：[field_id:2B LE][value:nB]
check_eq(md_bin_write_field(38, b"\x20\x03"),
         md_build_frame(0x12, struct.pack("<H", 38) + b"\x20\x03"), "write_field bytes")
check_eq(md_bin_write_field(38, 800, 2),
         md_build_frame(0x12, struct.pack("<H", 38) + struct.pack("<H", 800)), "write_field int")
check_raises(ValueError, md_bin_write_field, 38, 800)       # int 缺 value_len
check_raises(ValueError, md_bin_write_field, 38, b"\x01", 2)  # value_len 不符
check_raises(ValueError, md_bin_write_field, 0x10000, b"\x00")

# motor_raw：[ch][dir][pwm LE]
check_eq(md_bin_motor_raw(0, 0, 1000),
         md_build_frame(0x30, b"\x00\x00\xe8\x03"), "motor_raw(0,0,1000)")
check_eq(md_bin_motor_raw(3, 1, 0),
         md_build_frame(0x30, b"\x03\x01\x00\x00"), "motor_raw(3,1,0)")
check_raises(ValueError, md_bin_motor_raw, 4, 0, 0)
check_raises(ValueError, md_bin_motor_raw, 0, 2, 0)
check_raises(ValueError, md_bin_motor_raw, 0, 0, 1001)

# motor_ctrl（API.md §8 向量：DATA 段 == 64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00）
mc = md_bin_motor_ctrl(100, -200, 0, 300)
check_eq(mc[3:-1], bytes.fromhex("64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00"),
         "motor_ctrl(100,-200,0,300) DATA 段 == 官方向量")
check_eq(mc, md_build_frame(0x31, struct.pack("<4i", 100, -200, 0, 300)),
         "motor_ctrl 整帧 == build_frame(0x31, <4i)")
check_eq(md_bin_motor_ctrl(-2147483648, 2147483647, 0, 0)[3:7],
         b"\x00\x00\x00\x80", "int32 下边界")
check_raises(ValueError, md_bin_motor_ctrl, 2147483648, 0, 0, 0)

# subscribe / debug
check_eq(md_bin_subscribe(50), md_build_frame(0x40, b"\x32\x00"), "subscribe(50) DATA 32 00")
check_eq(md_bin_subscribe(20), md_build_frame(0x40, struct.pack("<H", 20)), "subscribe(20)")
check_eq(md_bin_debug_sbus(True), md_build_frame(0x43, b"\x01"), "debug_sbus(True)")
check_eq(md_bin_debug_sbus(0), md_build_frame(0x43, b"\x00"), "debug_sbus(0)")
check_eq(md_bin_debug_speed(1), md_build_frame(0x44, b"\x01"), "debug_speed(1)")
check_raises(ValueError, md_bin_subscribe, 65536)

# ---------------------------------------------------------------------------
# §7 解析层：ack / status / detect / sbus
# ---------------------------------------------------------------------------
print("== §7 解析层 ==")
# ack：err=0 成功，非 0 失败
check_eq(md_parse_ack(b"\x00").err, 0, "ack err=0x00")
check_eq(md_parse_ack(b"\xFF").err, 0xFF, "ack err=0xFF")
check_eq(md_parse_ack(b"\x01").err, 0x01, "ack err=0x01（非 0 视为失败）")
check_eq(md_parse_ack(b"\x00").cmd, None, "DATA 段输入时 cmd=None")
full_ack = md_build_frame(0x11, b"\x00")
a = md_parse_ack(full_ack)
check(a.cmd == 0x11 and a.err == 0x00, "完整 ACK 帧输入 → cmd/err 均取到")
check_raises(ValueError, md_parse_ack, b"\x00\x00")

# status 56B：字段与 §6.2 / §4 偏移一致，逐项断言
# （tgt 用 f32 可精确表示的值，保证解析后与字面量逐位相等）
enc = (0x7FFFFFFF, -2147483648, 12345, -67890)
tgt = (100.5, -200.25, 0.0, 3.140625)
rpm = (1000, -1000, 0, 32767)
fcnt, ocnt = 0xDEADBEEF, 123456789
p56 = struct.pack("<4i4f4iII", *enc, *tgt, *rpm, fcnt, ocnt)
check_eq(len(p56), 56, "56B payload 长度")
st56 = md_parse_status(p56)
check(st56.enc == enc, "56B enc 逐项一致")
check(st56.tgt == tgt, "56B tgt 逐项一致")
check(st56.rpm == rpm, "56B rpm 逐项一致")
check(st56.rpm_raw == (0, 0, 0, 0), "56B rpm_raw 恒为 0")
check(st56.sbus_frame_cnt == fcnt, "56B sbus_frame_cnt == 0xDEADBEEF")
check(st56.sbus_ok_cnt == ocnt, "56B sbus_ok_cnt 一致")
check(st56.extended == 0, "56B extended == 0")
# 偏移抽查：rpm@32、frame_cnt@48
check(struct.unpack_from("<i", p56, 32)[0] == rpm[0], "rpm[0] 位于 @32")
check(struct.unpack_from("<I", p56, 48)[0] == fcnt, "sbus_frame_cnt 位于 @48")

# status 72B：rpm_raw 插入 rpm 与计数之间
rpm_raw = (-5, 6, -7, 8)
p72 = struct.pack("<4i4f4i4iII", *enc, *tgt, *rpm, *rpm_raw, fcnt, ocnt)
check_eq(len(p72), 72, "72B payload 长度")
st72 = md_parse_status(p72)
check(st72.enc == enc and st72.tgt == tgt and st72.rpm == rpm,
      "72B enc/tgt/rpm 一致")
check(st72.rpm_raw == rpm_raw, "72B rpm_raw 逐项一致")
check(st72.sbus_frame_cnt == fcnt and st72.sbus_ok_cnt == ocnt, "72B 计数一致")
check(st72.extended == 1, "72B extended == 1")
check(struct.unpack_from("<i", p72, 48)[0] == rpm_raw[0], "72B rpm_raw[0] 位于 @48")
check(struct.unpack_from("<I", p72, 64)[0] == fcnt, "72B sbus_frame_cnt 位于 @64")
check_raises(ValueError, md_parse_status, p56 + b"\x00" * 4)   # 60B 非法

# detect：[proto][inv][baud LE]
check_eq(md_parse_detect(b"\x02\x01" + struct.pack("<I", 115200)),
         (2, 1, 115200), "detect proto=2 inv=1 baud=115200")
check_eq(md_parse_detect(b"\x00\x00" + struct.pack("<I", 0)).proto, 0, "detect 失败 proto=0")
check_raises(ValueError, md_parse_detect, b"\x02\x01\x00")

# sbus：16×uint16 LE
chans = tuple(range(16))
p_sbus = struct.pack("<16H", *chans)
check_eq(md_parse_sbus(p_sbus).ch, chans, "sbus 16 通道逐项一致")
check_eq(md_parse_sbus(p_sbus).ch[15], 15, "sbus ch15")
check_raises(ValueError, md_parse_sbus, p_sbus[:31])

# ---------------------------------------------------------------------------
# §8 config_t：解析 ↔ 打包往返无损 + 位域偏移抽查（API.md §8 向量）
# ---------------------------------------------------------------------------
print("== §8 config_t ==")
# 8.1 全字段差异化字典（浮点取 f32 精确可表示值，保证 dict 级 ==）
cfg = {
    "baud_rate": 115200,
    "cmd_timeout_ms": 500,
    "protocol": 2,
    "sbus_inv": 1,
    "ctrl_priority": 0,
    "control_mode": [1, 2, 0, 1],
    "motor_invert": [0, 1, 2, 3],
    "encoder_cpr": [500, 1000, 2000, 4000],
    "speed_period_ms": [5, 10, 15, 20],
    "speed_pid_type": [0, 1, 0, 1],
    "speed_olim": [1000, 900, 800, 700],
    "speed_kp": [1.5, 2.5, 3.5, 4.5],
    "speed_ki": [0.25, 0.5, 0.75, 1.0],
    "speed_kd": [0.125, 0.0625, 0.03125, 0.015625],
    "speed_ilim": [100.0, 200.0, 300.0, 400.0],
    "pos_period_ms": [6, 12, 18, 24],
    "pos_pid_type": [1, 0, 1, 0],
    "pos_kp": [5.5, 6.5, 7.5, 8.5],
    "pos_ki": [0.5, 0.625, 0.75, 0.875],
    "pos_kd": [0.25, 0.5, 0.75, 1.0],
    "pos_ilim": [50.0, 60.0, 70.0, 80.0],
    "pos_olim": [3000.0, 2000.0, 1000.0, 500.0],
    "pos_angle_cpr": [0, 500, 1000, 1500],
    "speed_filter_type": [0, 1, 2, 3],
    "speed_filter_window": [1, 8, 16, 32],
    "sbus_channel": [1, 4, 7, 16],
    "rc_dir_ch": [16, 3, 2, 1],
    "rc_map_mode": [1, 0, 1, 0],
    "rc_dir_en": [0, 1, 0, 1],
    "sbus_param": [1000, 2000, 3000, 4000],
    "sbus_range_min": 172,
    "sbus_range_max": 1811,
}
raw = md_pack_config(cfg)
check_eq(len(raw), 231, "pack_config 输出 231B")
check_eq(raw[:11], b"\x00" * 11, "受保护区（offset 0~10）置 0")
back = md_parse_config(raw)
check_eq(back, cfg, "config 全字段往返无损（含位域/float/数组）")

# 8.2 字节级偏移抽查（协议规范 §5）
check_eq(raw[11:15], struct.pack("<I", 115200), "@11 baud_rate u32")
check_eq(raw[15:17], struct.pack("<H", 500), "@15 cmd_timeout_ms u16")
check_eq(raw[17], 0x12, "@17 comm_flags = protocol2|sbus_inv<<4")
check_eq(raw[18], 0x49, "@18 control_mode 位域 = 0x49")
check_eq(raw[19], 0xE4, "@19 motor_invert 位域 = 0xE4")
check_eq(raw[20:28], struct.pack("<4H", 500, 1000, 2000, 4000), "@20 encoder_cpr")
check_eq(raw[46:50], struct.pack("<f", 1.5), "@46 speed_kp[0]")
check_eq(raw[110:118], struct.pack("<4H", 6, 12, 18, 24), "@110 pos_period_ms")
check_eq(raw[214:216], struct.pack("<H", 0xF630), "@214 sbus_channel_pack 位域")
check_eq(raw[216:218], struct.pack("<H", 0x012F), "@216 rc_dir_ch 位域")
check_eq(raw[218], 0xA5, "@218 rc_map_mode/rc_dir_en 位域")
check_eq(raw[219:227], struct.pack("<4H", 1000, 2000, 3000, 4000), "@219 sbus_param")
check_eq(raw[227:229], struct.pack("<H", 172), "@227 sbus_range_min")
check_eq(raw[229:231], struct.pack("<H", 1811), "@229 sbus_range_max")

# 8.3 任意 float 的字节级稳定性：pack(parse(pack(d))) == pack(d)
cfg2 = dict(cfg)
cfg2.update({
    "speed_kp": [0.1, 3.14159, -2.5e-3, 1e-30],
    "pos_olim": [123.456, -0.0001, 1e10, 3.3e-20],
})
raw2 = md_pack_config(cfg2)
check_eq(md_pack_config(md_parse_config(raw2)), raw2,
         "任意 float：parse 后重打包字节不变（f32 语义稳定）")

# 8.4 部分字段打包（缺省中性值）与 parse 长度校验
raw_empty = md_pack_config({})
check_eq(len(raw_empty), 231, "空 dict 打包 231B")
check_eq(raw_empty[17], 0x00, "空 dict comm_flags=0")
check_eq(md_parse_config(raw_empty)["sbus_channel"], [1, 1, 1, 1], "缺省通道=CH1")
check_raises(ValueError, md_parse_config, raw[:230])          # 长度不符
check_raises(ValueError, md_pack_config, "not-dict")
check_raises(KeyError, md_pack_config, {"no_such_field": 1})

# 8.5 越界校验
bad_cfg = dict(cfg)
bad_cfg["sbus_channel"] = [0, 1, 1, 1]
check_raises(ValueError, md_pack_config, bad_cfg)             # 通道 0 非法
bad_cfg["sbus_channel"] = [17, 1, 1, 1]
check_raises(ValueError, md_pack_config, bad_cfg)             # 通道 17 非法
bad_cfg["sbus_channel"] = [1, 1, 1, 1]
bad_cfg["control_mode"] = [4, 0, 0, 0]
check_raises(ValueError, md_pack_config, bad_cfg)             # 2bit 字段越界
bad_cfg["control_mode"] = [1, 0, 0, 0]
bad_cfg["baud_rate"] = -1
check_raises(ValueError, md_pack_config, bad_cfg)             # 负数 u32
bad_cfg["baud_rate"] = 115200
bad_cfg["speed_pid_type"] = [16, 0, 0, 0]
check_raises(ValueError, md_pack_config, bad_cfg)             # 4bit 字段越界

# ---------------------------------------------------------------------------
# §9 MDC 便捷类
# ---------------------------------------------------------------------------
print("== §9 MDC 类 ==")
mdc = MDC()
check_eq(mdc.crc8(b"123456789"), 0xF4, "MDC.crc8")
check_eq(mdc.build_frame(0x01, b""), b"\xAA\x01\x00\x15", "MDC.build_frame")
check_eq(mdc.ping(), md_bin_ping(), "MDC.ping")
check_eq(mdc.motor_ctrl(1, 2, 3, 4), md_bin_motor_ctrl(1, 2, 3, 4), "MDC.motor_ctrl")
check_eq(mdc.motor_raw(1, 0, 500), md_bin_motor_raw(1, 0, 500), "MDC.motor_raw")
check_eq(mdc.mode(1, "speed"), b"/mode 1 speed\n", "MDC.mode")
check_eq(mdc.enczero(2), b"/enczero 2\n", "MDC.enczero")
check_eq(mdc.version(), b"/version\n", "MDC.version")
check_eq(mdc.save(), b"/save\n", "MDC.save")
check_eq(mdc.write_param(cfg_demo), md_bin_write_param(cfg_demo), "MDC.write_param")
check_eq(mdc.write_field(38, 800, 2), md_bin_write_field(38, 800, 2), "MDC.write_field")
check_eq(mdc.subscribe(50), md_bin_subscribe(50), "MDC.subscribe")
check_eq(mdc.parse_status(p56), st56, "MDC.parse_status")
check_eq(mdc.parse_ack(b"\x00").err, 0, "MDC.parse_ack")
check_eq(mdc.parse_config(raw), cfg, "MDC.parse_config 往返")
check_eq(mdc.pack_config(cfg), raw, "MDC.pack_config")
check_eq(mdc.parse_sbus(p_sbus).ch, chans, "MDC.parse_sbus")
# MDC 流式解析
got9 = feed_all(mdc, frame_a)
check_eq(got9, [(0x30, b"\x00\x00\xe8\x03")], "MDC.feed 流式解析")
mdc.reset_parser()
check_eq(mdc.parser.buf, bytearray(), "MDC.reset_parser 清空缓冲")

# ---------------------------------------------------------------------------
# 汇总
# ---------------------------------------------------------------------------
print()
print("=" * 60)
print("断言统计：PASS = %d，FAIL = %d" % (PASS, FAIL))
if FAIL:
    print("失败明细：")
    for i, f in enumerate(FAILURES, 1):
        print("  %d) %s" % (i, f))
    print("结果：FAILED")
else:
    print("结果：ALL PASSED")
print("=" * 60)
sys.exit(1 if FAIL else 0)
