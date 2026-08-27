# -*- coding: utf-8 -*-
"""
mdc_lite_ctrl.py — 极简调用库（调用+回调接收，Python 参考实现）
=================================================================

**核心思想：上位机调参、下位机执行。** 在 `mdc_lite`（只管调用）基础上，
增加一个**流式状态接收器**：你逐字节喂入收到的数据，遇到下位机主动推送的
`0xF0 STATUS_REPORT` 时自动解析并调用你注册的**速度回调**（四通道 rpm）。

**只涉及 4 条命令：** 0x31 MOTOR_CTRL、0x40/0x41 SUBSCRIBE/UNSUBSCRIBE、0xF0 STATUS_REPORT。

**依赖：** 复用同目录 `mdc_lib.py`（打包/解析）与 `mdc_lite.py`（发送侧），
本文件只新增"回调接收器"。

**用法（串口由你实现，含回调）：**

    import serial
    from mdc_lite_ctrl import MDLite

    def on_speed(rpm):
        print("实时转速 rpm:", rpm)        # rpm = (m0, m1, m2, m3)

    ser = serial.Serial("COM5", 2000000)
    mdc = MDLite(on_speed)                 # 注册速度回调
    ser.write(mdc.subscribe(50))           # 先订阅（收速度的前提）

    while True:
        ser.write(mdc.ctrl(100, -200, 0, 300))    # 发送控制帧
        for b in ser.read(64):                    # 把收到的字节喂给接收器
            mdc.feed(b)                           # 0xF0 到达时自动回调 on_speed
"""

import mdc_lib as _md
import mdc_lite as _lite

__all__ = ["MDLite"]


class MDLite:
    """极简控制器：发送侧（复用 mdc_lite）+ 速度回调接收侧。

    示例::

        mdc = MDLite(on_speed=lambda rpm: print("rpm:", rpm))
        ser.write(mdc.subscribe(50))
        while True:
            ser.write(mdc.ctrl(100, 0, 0, 0))
            for b in ser.read(64):
                mdc.feed(b)
    """

    def __init__(self, on_speed=None):
        """注册速度回调 ``on_speed(rpm)``（rpm 为 4 元 int32 转速）。

        :param on_speed: 可调用对象，签名 ``on_speed(rpm0, rpm1, rpm2, rpm3)`` 或
                         接收 4 元组的 ``on_speed(rpm)``；为 None 时不派发。
        """
        self.on_speed = on_speed
        self._parser = _md.MDParser()       # 复用之 mdc_lib 流式解析器

    # ---- 发送侧（复用 mdc_lite，语义同 LITE.md §2）----
    def ctrl(self, m0, m1, m2, m3) -> bytes:
        """0x31 MOTOR_CTRL 控制帧 bytes（同 mdc_lite.ctrl）。"""
        return _lite.ctrl(m0, m1, m2, m3)

    def stop(self) -> bytes:
        """四通道全零控制帧 bytes（同 mdc_lite.stop）。"""
        return _lite.stop()

    def subscribe(self, interval_ms: int) -> bytes:
        """0x40 SUBSCRIBE 帧 bytes（同 mdc_lite.subscribe）。"""
        return _lite.subscribe(interval_ms)

    def unsubscribe(self) -> bytes:
        """0x41 UNSUBSCRIBE 帧 bytes（同 mdc_lite.unsubscribe）。"""
        return _lite.unsubscribe()

    # ---- 接收侧（回调）----
    def feed(self, byte: int):
        """喂入一个收到的字节（0~255）。

        收到完整且 CRC 通过的 ``0xF0 STATUS_REPORT``（56B/72B 自动兼容）时，
        解析四通道 rpm 并调用注册的 speed 回调；其他帧/噪声忽略。
        """
        r = self._parser.feed(byte)
        if r and r[0] == _md.MD_CMD_STATUS_REPORT:    # 0xF0
            st = _md.md_parse_status(r[1])
            if self.on_speed is not None:
                self.on_speed(st.rpm)                 # (rpm0, rpm1, rpm2, rpm3)

    def reset(self):
        """清空流式解析器缓冲（切换连接/重新同步时调用）。"""
        self._parser.reset()
