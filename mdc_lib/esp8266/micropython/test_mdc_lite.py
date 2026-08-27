# -*- coding: utf-8 -*-
"""test_mdc_lite.py — mdc_lite / mdc_lite_ctrl 一致性自检（MicroPython 零依赖版）。

不依赖任何第三方库，也不依赖 struct —— 仅用内置 bytes/bytearray 拼装验证向量。

运行：python test_mdc_lite.py   （在 mdc_lite.py / mdc_lite_ctrl.py / mdc_lib.py 同目录下）
通过打印 "ALL OK"；失败抛 AssertionError 并退出码 1。
"""

import sys

import mdc_lib
import mdc_lite
import mdc_lite_ctrl


def _ok(cond, msg):
    if not cond:
        raise AssertionError("FAIL: " + msg)
    print("ok -", msg)


def _hex(b):
    return " ".join("%02X" % x for x in b)


# ---- 小端拼装（纯移位，不依赖 struct）----
def _u16(v):
    return bytes((v & 0xFF, (v >> 8) & 0xFF))


def _u32(v):
    return bytes((v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF))


def _i32(v):
    return _u32(v & 0xFFFFFFFF)


def test_send_only():
    # 控制帧 DATA 段向量（LITE.md §2.2 / API.md §8）
    ctrl = mdc_lite.ctrl(100, -200, 0, 300)
    _ok(ctrl[1] == 0x31 and ctrl[2] == 16, "ctrl CMD=0x31 LEN=16")
    _ok(ctrl[3:19] == bytes.fromhex(
        "64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00"),
        "ctrl DATA == 64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00")

    # 订阅帧
    sub = mdc_lite.subscribe(50)
    _ok(sub[0] == 0xAA and sub[1] == 0x40 and sub[2] == 2 and sub[3] == 0x32
        and sub[4] == 0x00, "subscribe(50) == AA 40 02 32 00 <crc>")
    _ok(mdc_lib.md_crc8(sub[1:5]) == sub[5], "subscribe 帧 CRC 正确")

    # 停止 / 取消订阅
    _ok(mdc_lite.stop() == mdc_lite.ctrl(0, 0, 0, 0), "stop == ctrl(0,0,0,0)")
    _ok(mdc_lite.unsubscribe()[1] == 0x41, "unsubscribe CMD=0x41")

    # 与 mdc_lib 一致性
    _ok(mdc_lite.ctrl(1, 2, 3, 4) == mdc_lib.md_bin_motor_ctrl(1, 2, 3, 4),
        "ctrl 与 mdc_lib.md_bin_motor_ctrl 等价")

    # CRC 向量
    _ok(mdc_lib.md_crc8([0x01, 0x00]) == 0x15, "crc8([0x01,0x00]) == 0x15")
    _ok(mdc_lib.md_crc8(b"123456789") == 0xF4, "crc8(b'123456789') == 0xF4")
    print()


def _status56(rpm):
    """构造 56B STATUS_REPORT payload（tgt 置 0.0，rpm 用传入值）。"""
    enc = (0, 0, 0, 0)
    payload = b""
    for x in enc:
        payload += _i32(x)
    payload += b"\x00\x00\x00\x00" * 4            # tgt: 4 个 f32 0.0 = 16B
    for x in rpm:
        payload += _i32(x)
    payload += _u32(7)                            # sbus_frame_cnt
    payload += _u32(6)                            # sbus_ok_cnt
    assert len(payload) == 56, len(payload)
    return payload


def _status72(rpm):
    """构造 72B STATUS_REPORT payload（tgt 置 0.0，rpm_raw 置 0）。"""
    enc = (0, 0, 0, 0)
    rpm_raw = (0, 0, 0, 0)
    payload = b""
    for x in enc:
        payload += _i32(x)
    payload += b"\x00\x00\x00\x00" * 4            # tgt: 4 个 f32 0.0 = 16B
    for x in rpm:
        payload += _i32(x)
    for x in rpm_raw:
        payload += _i32(x)
    payload += _u32(1)                            # sbus_frame_cnt
    payload += _u32(1)                            # sbus_ok_cnt
    assert len(payload) == 72, len(payload)
    return payload


def test_ctrl_callback():
    got = []

    def on_speed(rpm):
        got.append(tuple(rpm))

    mdc = mdc_lite_ctrl.MDLite(on_speed)

    # 先喂噪声 + 0x31 控制帧，确认不触发回调
    for b in b"\xaa\x31\x10\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x11":
        mdc.feed(b)                                # 0x31 控制帧不应触发回调
    _ok(len(got) == 0, "非 0xF0 帧不触发回调")

    # 构造一帧 56B 状态帧并整帧喂入
    rpm = (1234, -567, 0, 9000)
    frame = mdc_lib.md_build_frame(0xF0, _status56(rpm))
    for b in frame:
        mdc.feed(b)
    _ok(len(got) == 1, "0xF0 帧触发一次回调")
    _ok(got[0] == rpm, "回调 rpm == %r，实际 %r" % (rpm, got[0]))

    # 72B 扩展帧也应识别
    got.clear()
    frame72 = mdc_lib.md_build_frame(0xF0, _status72((10, 20, 30, 40)))
    for b in frame72:
        mdc.feed(b)
    _ok(len(got) == 1 and got[0] == (10, 20, 30, 40), "72B 帧解析 rpm 正确")

    # 坏 CRC 帧不触发：整帧篡改最后一个（CRC）字节
    got.clear()
    bad = bytearray(frame)
    bad[-1] ^= 0xFF                               # 篡改 CRC 字节
    for b in bad:
        mdc.feed(b)
    _ok(len(got) == 0, "坏 CRC 帧不触发回调")
    print()


if __name__ == "__main__":
    try:
        test_send_only()
        test_ctrl_callback()
    except AssertionError as e:
        print(e)
        sys.exit(1)
    print("ALL OK")
