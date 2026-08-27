# -*- coding: utf-8 -*-
"""
mdc_lite.py — 极简调用库（只管调用 / send-only，完全独立实现）
=============================================================

**独立实现**：自带 CRC8、组帧与命令打包，**不依赖任何外部库**（不 import mdc_lib），
也用不到 `struct` / `machine` / `math` 等任何模块 —— 全部用内置 `bytes` / `bytearray`
与逐字节移位、四则运算完成，在任何 MicroPython 板或 CPython 上均可直接 import。

**核心思想：上位机调参、下位机执行。** 你只需要告诉下位机"发什么控制量"，
本模块把"要发送的帧字节"打包好交给你，由你自行 `uart.write()` 发送。**不解析任何回包**
—— 若需读回转速，请用 `mdc_lite_ctrl`（调用+回调接收）。

**只涉及 3 条命令**（帧格式 [0xAA][CMD][LEN][DATA...][CRC8]，CRC 范围 = CMD+LEN+DATA，
多项式 0x07 初值 0）：
    * 0x31 MOTOR_CTRL    四通道控制目标（下位机执行）
    * 0x40 SUBSCRIBE     开启状态周期上报（收速度的前提）
    * 0x41 UNSUBSCRIBE   关闭状态上报（善后）

**用法（串口由你实现）：**

    from machine import UART, Pin
    import mdc_lite

    uart = UART(2, baudrate=115200, tx=Pin(17), rx=Pin(16), timeout=50)
    uart.write(mdc_lite.subscribe(50))                # 先订阅（如需回读转速）
    uart.write(mdc_lite.ctrl(100, -200, 0, 300))      # 四通道目标值
    uart.write(mdc_lite.ctrl(0, 0, 0, 0))             # 归零
    uart.write(mdc_lite.unsubscribe())                # 退出前取消订阅（可选）

**各函数返回 `bytes`（完整二进制帧，含 SYNC+CRC8），直接 uart.write 即可。**
"""

# ---- 常量 ----
MD_SYNC = 0xAA            # 帧同步字
MD_MAX_DATA = 250         # DATA 段最大长度
MD_CRC8_POLY = 0x07       # CRC8 多项式（初值 0）
MD_CMD_MOTOR_CTRL = 0x31  # 四通道控制
MD_CMD_SUBSCRIBE = 0x40   # 订阅状态上报
MD_CMD_UNSUBSCRIBE = 0x41 # 取消订阅

__all__ = ["ctrl", "stop", "subscribe", "unsubscribe", "crc8"]


def crc8(data):
    """CRC8：多项式 0x07，初值 0，按位计算。校验向量 crc8([0x01,0x00])==0x15；
    crc8(b"123456789")==0xF4。

    :param data: bytes / bytearray / 0~255 整数列表。
    """
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ MD_CRC8_POLY) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def _u16le(v):
    """uint16 -> 2B 小端 bytes（纯移位，不用 struct）。"""
    return bytes((v & 0xFF, (v >> 8) & 0xFF))


def _i32le(v):
    """int32（含负数）-> 4B 小端 bytes（补码，纯移位，不用 struct）。

    :raises ValueError: 值超出 int32 范围（与 ``struct.pack("<4i", ...)`` 越界行为一致）。
    """
    if v < -0x80000000 or v > 0x7FFFFFFF:
        raise ValueError("值越出 int32 范围：%d" % v)
    u = v & 0xFFFFFFFF
    return bytes((u & 0xFF, (u >> 8) & 0xFF, (u >> 16) & 0xFF, (u >> 24) & 0xFF))


def _build_frame(cmd, data=b""):
    """组帧 [0xAA][CMD][LEN][DATA...][CRC8]，CRC=CMD+LEN+DATA。"""
    body = bytes((cmd, len(data))) + data
    return bytes((MD_SYNC,)) + body + bytes((crc8(body),))


def ctrl(m0, m1, m2, m3):
    """0x31 MOTOR_CTRL：四通道控制目标（int32 LE），返回完整帧 bytes。

    例：``ctrl(100, -200, 0, 300)`` 的 DATA 段 == 64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00。
    目标值含义随各通道控制模式：open=PWM(±1000)、speed=RPM、pos=0.1°。
    """
    return _build_frame(MD_CMD_MOTOR_CTRL,
                        _i32le(m0) + _i32le(m1) + _i32le(m2) + _i32le(m3))


def stop():
    """便捷：四通道全零控制帧（急停/退出前发送，0x31 DATA=16B 全零）。"""
    return ctrl(0, 0, 0, 0)


def subscribe(interval_ms):
    """0x40 SUBSCRIBE：开启状态周期上报，返回完整帧 bytes。

    :param interval_ms: 上报周期 ms，建议 >=20（固件钳位）。
    """
    return _build_frame(MD_CMD_SUBSCRIBE, _u16le(interval_ms))


def unsubscribe():
    """0x41 UNSUBSCRIBE：关闭状态上报，返回完整帧 bytes。"""
    return _build_frame(MD_CMD_UNSUBSCRIBE)
