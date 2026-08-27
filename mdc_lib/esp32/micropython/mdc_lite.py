# -*- coding: utf-8 -*-
"""
mdc_lite.py — 极简调用库（只管调用 / send-only，MicroPython 零依赖版）
======================================================================

**核心思想：上位机调参、下位机执行。** 你只需要告诉下位机"发什么控制量"，
本模块把"要发送的字节"打包好交给你，由你自行 `uart.write()` 发送。
**不解析任何回包** —— 若需读回转速，请用 `mdc_lite_ctrl`（调用+回调接收）。

**只涉及 3 条命令：**
    * 0x31 MOTOR_CTRL    四通道控制目标（下位机执行）
    * 0x40 SUBSCRIBE     开启状态周期上报（收速度的前提）
    * 0x41 UNSUBSCRIBE   关闭状态上报（善后）

**依赖：** 复用同目录 `mdc_lib.py`（API.md 的打包原语），本文件只是极薄封装。
纯 MicroPython **零依赖**（无 machine / ustruct / struct / math 等任何 import），
在任意 MicroPython 板或 CPython 上均可直接 import。

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

# 复用同目录 mdc_lib（极简层不重复实现协议）
import mdc_lib as _md

__all__ = [
    "ctrl", "stop", "subscribe", "unsubscribe",
]

# 目标值含义随通道控制模式（协议规范 §3.3）：
#   open = PWM(±1000)  speed = RPM   pos = 0.1°(±3600=±360.0°)


def ctrl(m0, m1, m2, m3):
    """0x31 MOTOR_CTRL：四通道控制目标（int32 LE），返回完整帧 bytes。

    例：``ctrl(100, -200, 0, 300)`` 的 DATA 段 == ``64 00 00 00 38 FF FF FF
    00 00 00 00 2C 01 00 00``。等价于 mdc_lib.md_bin_motor_ctrl(...)。
    """
    return _md.md_bin_motor_ctrl(m0, m1, m2, m3)


def stop():
    """便捷：四通道全零控制帧（急停/退出前发送，0x31 DATA=16B 全零）。"""
    return ctrl(0, 0, 0, 0)


def subscribe(interval_ms):
    """0x40 SUBSCRIBE：开启状态周期上报，返回完整帧 bytes。

    :param interval_ms: 上报周期 ms，建议 >=20（固件钳位）。等价于 mdc_lib.md_bin_subscribe。
    """
    return _md.md_bin_subscribe(interval_ms)


def unsubscribe():
    """0x41 UNSUBSCRIBE：关闭状态上报，返回完整帧 bytes。"""
    return _md.md_bin_unsubscribe()
