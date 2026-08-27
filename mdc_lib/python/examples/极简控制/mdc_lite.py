# -*- coding: utf-8 -*-
"""
mdc_lite.py — 极简调用库（只管调用 / send-only，独立实现）
=============================================================

**独立实现**：自带 CRC8、组帧与命令打包，**不依赖任何外部库**（不 import mdc_lib）。
只用到 Python 标准库 `struct`。

**核心思想：上位机调参、下位机执行。** 你只需要告诉下位机"发什么控制量"，
本模块把"要发送的帧字节"打包好交给你，由你自行 `ser.write()` 发送。不解析回包。

**只涉及 3 条命令**（帧格式 [0xAA][CMD][LEN][DATA][CRC8]，CRC 范围=CMD+LEN+DATA，多项式 0x07 初值 0）：
    * 0x31 MOTOR_CTRL    四通道控制目标（下位机执行）
    * 0x40 SUBSCRIBE     开启状态周期上报（收速度的前提）
    * 0x41 UNSUBSCRIBE   关闭状态上报（善后）

**用法（串口由你实现）：**

    import serial, mdc_lite
    ser = serial.Serial("COM5", 2000000)
    ser.write(mdc_lite.subscribe(50))             # 先订阅（如需回读转速）
    ser.write(mdc_lite.ctrl(100, -200, 0, 300))   # 四通道目标值
    ser.write(mdc_lite.ctrl(0, 0, 0, 0))          # 归零
    ser.write(mdc_lite.unsubscribe())             # 退出前取消订阅（可选）

**如需读回转速，见 `mdc_lite_ctrl`（调用+回调接收，同为独立实现）。**
"""

import struct

# ---- 常量 ----
MD_SYNC = 0xAA            # 帧同步字
MD_MAX_DATA = 250         # DATA 段最大长度
MD_CRC8_POLY = 0x07       # CRC8 多项式（初值 0）
MD_CMD_MOTOR_CTRL = 0x31  # 四通道控制
MD_CMD_SUBSCRIBE = 0x40   # 订阅状态上报
MD_CMD_UNSUBSCRIBE = 0x41 # 取消订阅

__all__ = ["ctrl", "stop", "subscribe", "unsubscribe"]


def _crc8(data):
    """CRC8：多项式 0x07，初值 0，按位计算。校验向量 crc8([0x01,0x00])==0x15。"""
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ MD_CRC8_POLY) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def _build_frame(cmd, data=b""):
    """组帧 [0xAA][CMD][LEN][DATA...][CRC8]，CRC=CMD+LEN+DATA。"""
    body = bytes((cmd, len(data))) + data
    return bytes((MD_SYNC,)) + body + bytes((_crc8(body),))


def ctrl(m0, m1, m2, m3):
    """0x31 MOTOR_CTRL：四通道控制目标（int32 LE），返回完整帧 bytes。

    例：``ctrl(100, -200, 0, 300)`` 的 DATA 段 == 64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00。
    """
    return _build_frame(MD_CMD_MOTOR_CTRL, struct.pack("<4i", m0, m1, m2, m3))


def stop():
    """便捷：四通道全零控制帧（急停/退出前发送，0x31 DATA=16B 全零）。"""
    return ctrl(0, 0, 0, 0)


def subscribe(interval_ms):
    """0x40 SUBSCRIBE：开启状态周期上报，返回完整帧 bytes。

    :param interval_ms: 上报周期 ms，建议 >=20（固件钳位）。
    """
    return _build_frame(MD_CMD_SUBSCRIBE, struct.pack("<H", interval_ms))


def unsubscribe():
    """0x41 UNSUBSCRIBE：关闭状态上报，返回完整帧 bytes。"""
    return _build_frame(MD_CMD_UNSUBSCRIBE)
