# -*- coding: utf-8 -*-
"""
mdc_lite_ctrl.py — 极简调用库（调用+回调接收，独立实现）
=========================================================

**独立实现**：发送侧复用同目录独立库 `mdc_lite`；接收侧自带**流式解析器**与
**0xF0 STATUS_REPORT 解析**（提取四通道 rpm），**不依赖完整库 mdc_lib**。

**核心思想：上位机调参、下位机执行。** 在 `mdc_lite`（只管调用）基础上叠加一个
**速度回调接收器**：你逐字节喂入收到的数据，遇到下位机主动推送的 `0xF0` 时自动解析并
调用注册的**速度回调**（四通道 rpm）。

**只涉及 4 条命令：** 0x31 MOTOR_CTRL、0x40/0x41 SUBSCRIBE/UNSUBSCRIBE、0xF0 STATUS_REPORT。

**用法（串口由你实现，含回调）：**

    import serial
    from mdc_lite_ctrl import MDLite
    def on_speed(rpm): print("实时转速 rpm:", rpm)     # rpm = (m0, m1, m2, m3)
    ser = serial.Serial("COM5", 2000000)
    mdc = MDLite(on_speed)
    ser.write(mdc.subscribe(50))
    while True:
        ser.write(mdc.ctrl(100, -200, 0, 300))
        for b in ser.read(64): mdc.feed(b)            # 0xF0 到达时自动回调 on_speed
"""

import struct

import mdc_lite   # 同目录独立极简库（send-only，非完整库）

# 只关心的命令/帧
MD_CMD_STATUS_REPORT = 0xF0
MD_MAX_DATA = 250
MD_PARSER_BUF = 256

__all__ = ["MDLite"]


def _crc8(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc


class _StatusParser:
    """流式解析器：逐字节喂入，自动找 0xAA 同步 + CRC8 校验；仅关注 0xF0。"""

    def __init__(self, max_data=MD_MAX_DATA):
        self.max_data = max_data
        self.max_frame = 4 + max_data
        self.buf = bytearray()

    def reset(self):
        self.buf.clear()

    def feed(self, byte):
        self.buf.append(byte)
        while True:
            idx = self.buf.find(b"\xAA")
            if idx < 0:
                self.buf.clear()
                return None
            if idx > 0:
                del self.buf[:idx]
            if len(self.buf) < 3:
                return None
            plen = self.buf[2]
            if plen > self.max_data:
                del self.buf[0]
                continue
            need = 3 + plen + 1
            if len(self.buf) < need:
                if len(self.buf) >= self.max_frame:
                    del self.buf[0]
                    continue
                return None
            body = bytes(self.buf[1:3 + plen])
            if _crc8(body) == self.buf[3 + plen]:
                cmd = self.buf[1]
                payload = bytes(self.buf[3:3 + plen])
                del self.buf[:need]
                return (cmd, payload)
            del self.buf[0]


def _parse_rpm(payload):
    """从 0xF0 DATA 段（56B 常规 / 72B 扩展）解析四通道 rpm，返回 tuple。"""
    n = len(payload)
    if n == 56:
        return struct.unpack_from("<4i", payload, 32)
    if n == 72:
        return struct.unpack_from("<4i", payload, 32)
    raise ValueError("未知 STATUS_REPORT 长度：%d（应为 56 或 72）" % n)


class MDLite:
    """极简控制器：发送侧（复用 mdc_lite）+ 速度回调接收侧。

    示例::

        mdc = MDLite(on_speed=lambda rpm: print("rpm:", rpm))
        ser.write(mdc.subscribe(50))
        while True:
            ser.write(mdc.ctrl(100, 0, 0, 0))
            for b in ser.read(64):
                mdc.feed(b)     # 0xF0 到达自动回调 on_speed(rpm)
    """

    def __init__(self, on_speed=None):
        """注册速度回调 ``on_speed(rpm)``（rpm 为 4 元组 int32 转速）。"""
        self.on_speed = on_speed
        self._parser = _StatusParser()

    # ---- 发送侧（复用 mdc_lite）----
    def ctrl(self, m0, m1, m2, m3) -> bytes:
        return mdc_lite.ctrl(m0, m1, m2, m3)

    def stop(self) -> bytes:
        return mdc_lite.stop()

    def subscribe(self, interval_ms: int) -> bytes:
        return mdc_lite.subscribe(interval_ms)

    def unsubscribe(self) -> bytes:
        return mdc_lite.unsubscribe()

    # ---- 接收侧（回调）----
    def feed(self, byte: int):
        r = self._parser.feed(byte)
        if r and r[0] == MD_CMD_STATUS_REPORT:      # 0xF0
            rpm = _parse_rpm(r[1])
            if self.on_speed is not None:
                self.on_speed(rpm)

    def reset(self):
        self._parser.reset()
