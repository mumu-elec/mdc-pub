# -*- coding: utf-8 -*-
"""test_mdc_lite.py — mdc_lite / mdc_lite_ctrl 一致性自检（完全独立实现，零依赖）。

不依赖第三方库，也不依赖 struct / mdc_lib —— 验证 mdc_lite 是独立实现。
本测试只用内置 bytes/bytearray 手工拼装验证向量。

运行：python test_mdc_lite.py   （在 mdc_lite.py / mdc_lite_ctrl.py 同目录下）
通过打印 "ALL OK"；失败抛 AssertionError 并退出码 1。
"""

import sys

import mdc_lite
import mdc_lite_ctrl


def _ok(cond, msg):
    if not cond:
        raise AssertionError("FAIL: " + msg)
    print("ok -", msg)


# ---- 小端拼装 / CRC（纯移位，不依赖 struct）----
def _u32(v):
    return bytes((v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF))


def _i32(v):
    return _u32(v & 0xFFFFFFFF)


def _build_frame(cmd, data):
    body = bytes((cmd, len(data))) + data
    return bytes((0xAA,)) + body + bytes((mdc_lite.crc8(body),))


def _status56(rpm):
    """构造 56B STATUS_REPORT payload（tgt 置 0.0，rpm 用传入值）。"""
    payload = b""
    for _ in range(4):
        payload += _i32(0)                     # enc: 16B
    payload += b"\x00\x00\x00\x00" * 4         # tgt: 4 个 f32 0.0 = 16B
    for x in rpm:
        payload += _i32(x)                     # rpm: 16B（偏移 32）
    payload += _u32(7)                         # sbus_frame_cnt
    payload += _u32(6)                         # sbus_ok_cnt
    assert len(payload) == 56, len(payload)
    return payload


def _status72(rpm):
    """构造 72B STATUS_REPORT payload（tgt 置 0.0，rpm_raw 置 0）。"""
    payload = b""
    for _ in range(4):
        payload += _i32(0)                     # enc: 16B
    payload += b"\x00\x00\x00\x00" * 4         # tgt: 16B
    for x in rpm:
        payload += _i32(x)                     # rpm: 16B（偏移 32）
    for _ in range(4):
        payload += _i32(0)                     # rpm_raw: 16B
    payload += _u32(1)                         # sbus_frame_cnt
    payload += _u32(1)                         # sbus_ok_cnt
    assert len(payload) == 72, len(payload)
    return payload


def test_send_only():
    # CRC 向量（LITE.md §6）
    _ok(mdc_lite.crc8([0x01, 0x00]) == 0x15, "crc8([0x01,0x00]) == 0x15")
    _ok(mdc_lite.crc8(b"123456789") == 0xF4, "crc8(b'123456789') == 0xF4")

    # 控制帧 DATA 段向量（LITE.md §2.2 / API.md §8）
    ctrl = mdc_lite.ctrl(100, -200, 0, 300)
    _ok(ctrl[1] == 0x31 and ctrl[2] == 16, "ctrl CMD=0x31 LEN=16")
    _ok(ctrl[3:19] == bytes.fromhex(
        "64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00"),
        "ctrl DATA == 64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00")
    _ok(mdc_lite.crc8(ctrl[1:19]) == ctrl[19], "ctrl 帧 CRC 正确")

    # 订阅帧（LITE.md §2.2：AA 40 02 32 00 <crc>）
    sub = mdc_lite.subscribe(50)
    _ok(sub[:5] == bytes.fromhex("AA 40 02 32 00"), "subscribe(50) == AA 40 02 32 00 <crc>")
    _ok(sub == bytes.fromhex("AA 40 02 32 00 9E"),
        "subscribe(50) 整帧 == AA 40 02 32 00 9E")

    # 停止 / 取消订阅
    _ok(mdc_lite.stop() == mdc_lite.ctrl(0, 0, 0, 0), "stop == ctrl(0,0,0,0)")
    _ok(mdc_lite.stop() == bytes.fromhex("AA 31 10 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 3F"),
        "stop 整帧 == AA 31 10 <16B 全零> 3F")
    _ok(mdc_lite.unsubscribe() == bytes.fromhex("AA 41 00 4E"), "unsubscribe == AA 41 00 4E")
    print()


def test_ctrl_callback():
    got = []

    def on_speed(rpm):
        got.append(tuple(rpm))

    mdc = mdc_lite_ctrl.MDLite(on_speed)

    # 先喂噪声 + 一条合法 0x31 控制帧，确认不触发回调
    for b in b"\x00\x01\x02" + mdc_lite.ctrl(0, 0, 0, 0):
        mdc.feed(b)                                # 0x31 控制帧不应触发回调
    mdc.reset()
    _ok(len(got) == 0, "非 0xF0 帧不触发回调")

    # 构造一帧 56B 状态帧并整帧喂入
    rpm = (1234, -567, 0, 9000)
    frame = _build_frame(0xF0, _status56(rpm))
    for b in frame:
        mdc.feed(b)
    _ok(len(got) == 1, "0xF0 帧触发一次回调")
    _ok(got[0] == rpm, "回调 rpm == %r，实际 %r" % (rpm, got[0]))

    # 72B 扩展帧也应识别
    got.clear()
    frame72 = _build_frame(0xF0, _status72((10, 20, 30, 40)))
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

    # 噪声前缀：在完整帧前加垃圾字节，仍应只触发一次回调
    got.clear()
    for b in b"garbage\x00\x01\x02" + frame:
        mdc.feed(b)
    _ok(len(got) == 1 and got[0] == rpm, "噪声前缀 + 0xF0 帧仍解析正确")
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
