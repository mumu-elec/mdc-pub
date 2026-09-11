# -*- coding: utf-8 -*-
"""
mdc_lite_ctrl.py — 极简调用库（调用+回调接收，完全独立实现）
============================================================

**独立实现**：发送侧复用同族独立极简库 `mdc_lite`（非完整库 mdc_lib）；接收侧自带
**流式解析器**与 **0xF0 STATUS_REPORT 解析**（提取四通道 rpm）。`mdc_lite_ctrl`
本身只 `import mdc_lite`，不 import `mdc_lib`，也不用 `struct` / `machine` / `math`。

**核心思想：上位机调参、下位机执行。** 在 `mdc_lite`（只管调用）基础上叠加一个
**速度回调接收器**：你逐字节喂入收到的数据，遇到下位机主动推送的 `0xF0` 时自动解析并
调用注册的**速度回调**（四通道 rpm）。

**只涉及 4 条命令：** 0x31 MOTOR_CTRL、0x40/0x41 SUBSCRIBE/UNSUBSCRIBE、0xF0 STATUS_REPORT。

**用法（串口由你实现，含回调）：**

    from machine import UART, Pin
    from mdc_lite_ctrl import MDLite

    def on_speed(rpm):
        print("实时转速 rpm:", rpm)            # rpm = [m0, m1, m2, m3]

    uart = UART(2, baudrate=115200, tx=Pin(17), rx=Pin(16), timeout=50)
    mdc = MDLite(on_speed)                     # 注册速度回调
    uart.write(mdc.subscribe(50))              # 先订阅（收速度的前提）

    while True:
        uart.write(mdc.ctrl(100, -200, 0, 300))   # 发送控制帧
        n = uart.any()
        if n:
            for b in uart.read(n):                # 把收到的字节喂给接收器
                mdc.feed(b)                       # 0xF0 到达时自动回调 on_speed
"""

import mdc_lite   # 同族独立极简库（send-only，非完整库 mdc_lib）

# 只关心的命令/帧
MD_CMD_STATUS_REPORT = 0xF0
MD_MAX_DATA = 248
MD_PARSER_BUF = 256

__all__ = ["MDLite"]


def _get_i32(b, off):
    """读取 4B 小端 int32（补码）。"""
    u = (b[off] | (b[off + 1] << 8) | (b[off + 2] << 16) | (b[off + 3] << 24)) & 0xFFFFFFFF
    return u - 0x100000000 if u >= 0x80000000 else u


def _parse_rpm(payload):
    """从 0xF0 DATA 段（56B 常规 / 72B 扩展）解析四通道 rpm，返回 4 元 tuple。

    rpm 在 56B 与 72B 两种形态下均位于 DATA 段偏移 32（前 16B enc + 16B tgt）。
    """
    n = len(payload)
    if n == 56:
        return (_get_i32(payload, 32), _get_i32(payload, 36),
                _get_i32(payload, 40), _get_i32(payload, 44))
    if n == 72:
        return (_get_i32(payload, 32), _get_i32(payload, 36),
                _get_i32(payload, 40), _get_i32(payload, 44))
    raise ValueError("未知 STATUS_REPORT 长度：%d（应为 56 或 72）" % n)


class _StatusParser:
    """流式解析器：逐字节喂入，自动找 0xAA 同步 + CRC8 校验；仅解析 0xF0 帧。

    行为（与 mdc_lib.MDParser 一致，独立实现，不依赖 mdc_lib）：
        * 滑动窗口找 0xAA；LEN > max_data 时丢弃该同步字重扫
        * CRC 失败丢弃该字节继续向后搜索，可容忍串口杂散字节
        * 收到完整且 CRC 通过的一帧时返回 ``(cmd, payload)``，否则返回 None
        * 未消费的剩余字节保留在内部缓冲，适配连续上报流
    """

    def __init__(self, max_data=MD_MAX_DATA):
        self.max_data = max_data
        self.max_frame = 4 + max_data      # 一帧最多 4 + max_data 字节
        self.buf = bytearray()

    def reset(self):
        self.buf.clear()

    def feed(self, byte):
        self.buf.append(byte)
        while True:
            idx = self.buf.find(b"\xAA")
            if idx < 0:                    # 无同步字：整段都是噪声
                self.buf.clear()
                return None
            if idx > 0:                    # 丢弃同步字前的杂散字节
                del self.buf[:idx]
            if len(self.buf) < 3:          # 至少需要 CMD + LEN 两个字节
                return None
            plen = self.buf[2]             # DATA 段长度
            if plen > self.max_data:       # LEN 非法：丢弃该同步字重扫
                del self.buf[0]
                continue
            need = 3 + plen + 1            # CMD+LEN+DATA+CRC
            if len(self.buf) < need:       # 整帧未收完：继续等待
                if len(self.buf) >= self.max_frame:   # 缓冲已满仍凑不齐：丢弃
                    del self.buf[0]
                    continue
                return None
            body = bytes(self.buf[1:3 + plen])   # CRC 计算范围
            if mdc_lite.crc8(body) == self.buf[3 + plen]:
                cmd = self.buf[1]
                payload = bytes(self.buf[3:3 + plen])
                del self.buf[:need]        # 消费整帧，剩余字节保留
                return (cmd, payload)
            del self.buf[0]                # CRC 失败：丢弃同步字继续扫描


class MDLite:
    """极简控制器：发送侧（复用 mdc_lite）+ 速度回调接收侧。

    示例::

        mdc = MDLite(on_speed=lambda rpm: print("rpm:", rpm))
        uart.write(mdc.subscribe(50))
        while True:
            uart.write(mdc.ctrl(100, 0, 0, 0))
            n = uart.any()
            if n:
                for b in uart.read(n):
                    mdc.feed(b)     # 0xF0 到达自动回调 on_speed(rpm)
    """

    def __init__(self, on_speed=None):
        """注册速度回调 ``on_speed(rpm)``（rpm 为 4 元组 int32 转速）。

        :param on_speed: 可调用对象，签名 ``on_speed(rpm)``；为 None 时不派发。
        """
        self.on_speed = on_speed
        self._parser = _StatusParser()

    # ---- 发送侧（复用 mdc_lite，语义同 LITE.md §2）----
    def ctrl(self, m0, m1, m2, m3):
        """0x31 MOTOR_CTRL 控制帧 bytes（同 mdc_lite.ctrl）。"""
        return mdc_lite.ctrl(m0, m1, m2, m3)

    def stop(self):
        """四通道全零控制帧 bytes（同 mdc_lite.stop）。"""
        return mdc_lite.stop()

    def subscribe(self, interval_ms):
        """0x40 SUBSCRIBE 帧 bytes（同 mdc_lite.subscribe）。"""
        return mdc_lite.subscribe(interval_ms)

    def unsubscribe(self):
        """0x41 UNSUBSCRIBE 帧 bytes（同 mdc_lite.unsubscribe）。"""
        return mdc_lite.unsubscribe()

    # ---- 接收侧（回调）----
    def feed(self, byte):
        """喂入一个收到的字节（0~255 的整数）。

        收到完整且 CRC 通过的 ``0xF0 STATUS_REPORT``（56B/72B 自动兼容）时，
        解析四通道 rpm 并调用注册的 speed 回调；其他帧/噪声忽略。
        """
        r = self._parser.feed(byte)
        if r and r[0] == MD_CMD_STATUS_REPORT:      # 0xF0
            rpm = _parse_rpm(r[1])
            if self.on_speed is not None:
                self.on_speed(rpm)

    def reset(self):
        """清空流式解析器缓冲（切换连接/重新同步时调用）。"""
        self._parser.reset()
