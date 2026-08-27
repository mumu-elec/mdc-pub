# -*- coding: utf-8 -*-
"""test_mdc_lite.py — mdc_lite / mdc_lite_ctrl 一致性自测（Python 参考实现）。

运行：python test_mdc_lite.py    （在 python/ 目录下）
通过打印 "ALL OK"；失败抛 AssertionError / 打印失败项并退出码 1。
"""

import struct
import sys

import mdc_lib
import mdc_lite
import mdc_lite_ctrl


def _ok(cond, msg):
    if not cond:
        raise AssertionError("FAIL: " + msg)
    print("ok -", msg)


def _hex(b):
    return b.hex(" ")


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
    print()


def _make_status56(enc, tgt, rpm, frame_cnt, ok_cnt):
    payload = (struct.pack("<4i", *enc) +
               struct.pack("<4f", *tgt) +
               struct.pack("<4i", *rpm) +
               struct.pack("<II", frame_cnt, ok_cnt))
    assert len(payload) == 56, len(payload)
    return mdc_lib.md_build_frame(0xF0, payload)


def test_ctrl_callback():
    got = []

    def on_speed(rpm):
        got.append(tuple(rpm))

    mdc = mdc_lite_ctrl.MDLite(on_speed)

    # 先喂噪声 + 坏 CRC 帧，确认不触发
    for b in b"\xaa\x31\x10\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x11":
        mdc.feed(b)                       # 0x31 控制帧不应触发回调
    _ok(len(got) == 0, "非 0xF0 帧不触发回调")

    # 构造一帧 56B 状态帧并整帧喂入
    rpm = (1234, -567, 0, 9000)
    frame = _make_status56((100, 200, 300, 400), (10.0, 20.0, 0.0, 5.0),
                           rpm, 7, 6)
    for b in frame:
        mdc.feed(b)
    _ok(len(got) == 1, "0xF0 帧触发一次回调")
    _ok(got[0] == rpm, "回调 rpm == %r，实际 %r" % (rpm, got[0]))

    # 72B 扩展帧也应识别
    got.clear()
    payload72 = (struct.pack("<4i", 0, 0, 0, 0) +
                 struct.pack("<4f", 0, 0, 0, 0) +
                 struct.pack("<4i", 10, 20, 30, 40) +
                 struct.pack("<4i", 11, 21, 31, 41) +
                 struct.pack("<II", 1, 1))
    assert len(payload72) == 72, len(payload72)
    frame72 = mdc_lib.md_build_frame(0xF0, payload72)
    for b in frame72:
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
