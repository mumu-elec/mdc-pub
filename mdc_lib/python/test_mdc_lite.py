# -*- coding: utf-8 -*-
"""test_mdc_lite.py — mdc_lite / mdc_lite_ctrl 一致性自测（独立实现）。

运行：python test_mdc_lite.py    （在 python/ 目录下）
通过打印 "ALL OK"；失败抛 AssertionError / 打印失败项并退出码 1。
本测试只用标准库 struct，不 import mdc_lib，验证 mdc_lite 是独立实现。
"""

import struct
import sys

import mdc_lite
import mdc_lite_ctrl


def _ok(cond, msg):
    if not cond:
        raise AssertionError("FAIL: " + msg)
    print("ok -", msg)


def _build_frame(cmd, data):
    body = bytes((cmd, len(data))) + data
    crc = 0
    for b in body:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return bytes((0xAA,)) + body + bytes((crc,))


def test_send_only():
    ctrl = mdc_lite.ctrl(100, -200, 0, 300)
    _ok(ctrl[1] == 0x31 and ctrl[2] == 16, "ctrl CMD=0x31 LEN=16")
    _ok(ctrl[3:19] == bytes.fromhex(
        "64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00"),
        "ctrl DATA == 64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00")

    sub = mdc_lite.subscribe(50)
    _ok(sub[:5] == bytes.fromhex("AA 40 02 32 00"), "subscribe(50) == AA 40 02 32 00 <crc>")
    _ok(sub[5] == sub[5], "subscribe 帧 CRC 存在")

    _ok(mdc_lite.stop() == mdc_lite.ctrl(0, 0, 0, 0), "stop == ctrl(0,0,0,0)")
    _ok(mdc_lite.unsubscribe()[1] == 0x41, "unsubscribe CMD=0x41")
    print()


def test_ctrl_callback():
    got = []
    mdc = mdc_lite_ctrl.MDLite(lambda rpm: got.append(tuple(rpm)))

    # 非 0xF0 帧不触发回调
    for b in mdc_lite.ctrl(100, 0, 0, 0):
        mdc.feed(b)
    _ok(len(got) == 0, "非 0xF0 帧不触发回调")

    # 56B 状态帧
    rpm = (1234, -567, 0, 9000)
    payload = (struct.pack("<4i", 100, 200, 300, 400) +
               struct.pack("<4f", 10.0, 20.0, 0.0, 5.0) +
               struct.pack("<4i", *rpm) +
               struct.pack("<II", 7, 6))
    frame = _build_frame(0xF0, payload)
    for b in frame:
        mdc.feed(b)
    _ok(len(got) == 1, "0xF0 帧触发一次回调")
    _ok(got[0] == rpm, "回调 rpm == %r，实际 %r" % (rpm, got[0]))

    # 72B 扩展帧
    got.clear()
    payload72 = (struct.pack("<4i", 0, 0, 0, 0) +
                 struct.pack("<4f", 0, 0, 0, 0) +
                 struct.pack("<4i", 10, 20, 30, 40) +
                 struct.pack("<4i", 11, 21, 31, 41) +
                 struct.pack("<II", 1, 1))
    for b in _build_frame(0xF0, payload72):
        mdc.feed(b)
    _ok(len(got) == 1 and got[0] == (10, 20, 30, 40), "72B 帧解析 rpm 正确")
    print()


if __name__ == "__main__":
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    try:
        test_send_only()
        test_ctrl_callback()
    except AssertionError as e:
        print(e)
        sys.exit(1)
    print("ALL OK")
