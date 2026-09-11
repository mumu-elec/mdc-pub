# -*- coding: utf-8 -*-
"""
mdc.py — Motor Driver Controller (MDC) v2 协议最小 Python 库

对应《技术手册》§5 通信协议。
仅依赖 pyserial（pip install pyserial）。串口相关功能在 Mdc 类内部按需加载，
即使未安装 pyserial 也可以 import 本模块做帧构建/CRC 计算与单元自测。

用法见同目录 01~05 示例脚本。
"""

import struct
import time

SYNC = 0xAA            # 帧头（§5.3）
USB_BAUD = 2000000     # USB (USART6) 固定 2000000-8N1，不可更改（§3.4）

# 二进制命令字（§5.5）
CMD_PING = 0x01
CMD_READ_PARAM = 0x10
CMD_WRITE_PARAM = 0x11
CMD_WRITE_FIELD = 0x12
CMD_SAVE_EEPROM = 0x20
CMD_LOAD_EEPROM = 0x21
CMD_FACTORY_RESET = 0x22
CMD_MOTOR_RAW = 0x30
CMD_MOTOR_CTRL = 0x31
CMD_MOTOR_JOG = 0x32
CMD_SUBSCRIBE = 0x40
CMD_UNSUBSCRIBE = 0x41
CMD_DEBUG_SBUS = 0x43
CMD_DEBUG_SPEED = 0x44
CMD_ENTER_BL = 0x52
CMD_REBOOT = 0x53
CMD_STATUS_REPORT = 0xF0
CMD_DETECT_REPORT = 0xF1
CMD_SBUS_DATA = 0xF2

CONFIG_SIZE = 248      # config_t 大小（§5.7）

try:
    import serial  # pyserial
except ImportError:
    serial = None


def crc8(data):
    """CRC8-ATM：多项式 0x07，初值 0x00，不反射，无异或输出（§5.3）。"""
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if (c & 0x80) else ((c << 1) & 0xFF)
    return c


def build_frame(cmd, data=b""):
    """构建一帧：[0xAA][CMD][LEN][DATA...][CRC8]，CRC 覆盖 CMD+LEN+DATA。"""
    if len(data) > 250:
        raise ValueError("DATA 最长 250 字节")
    body = bytes([cmd, len(data)]) + bytes(data)
    return bytes([SYNC]) + body + bytes([crc8(body)])


def parse_stream(buf):
    """
    从字节流 buf（bytes/bytearray）中解析第一帧。
    返回 (cmd, data, consumed)；没有完整帧时返回 (None, None, 丢弃的字节数)。
    """
    i = 0
    while i < len(buf):
        if buf[i] != SYNC:
            i += 1
            continue
        if len(buf) - i < 4:
            break
        ln = buf[i + 2]
        need = 4 + ln
        if len(buf) - i < need:
            break
        body = bytes(buf[i + 1:i + 3 + ln])
        crc = buf[i + 3 + ln]
        if crc8(body) == crc:
            return buf[i + 1], bytes(buf[i + 3:i + 3 + ln]), i + need
        i += 1  # CRC 不符，跳过这个 0xAA 继续扫描
    return None, None, i


class Mdc(object):
    """一台 MDC 驱动板的串口会话（USB 虚拟串口）。"""

    def __init__(self, port, baud=USB_BAUD, timeout=1.0):
        if serial is None:
            raise SystemExit("需要 pyserial：pip install pyserial")
        try:
            self.ser = serial.Serial(port, baud, bytesize=8, parity="N",
                                     stopbits=1, timeout=timeout)
        except Exception as e:
            raise SystemExit(
                "无法打开串口 %s: %s\n"
                "提示：设备管理器确认 CH340 端口号；波特率固定 %d；\n"
                "USB 仅通信不供电——请确认 DC 电源已接（§2.2/§6.1）" % (port, e, baud))
        self.timeout = timeout
        self._rxbuf = bytearray()

    def close(self):
        self.ser.close()

    # ---------- 文本指令（§5.2） ----------

    def cmd(self, line, idle=0.15, max_wait=1.0):
        """
        发送一条文本指令（自动补 \\n），收集回复直到 '> ' 提示符。
        注意：若已 SUBSCRIBE 开启遥测流，文本回复会被数据流淹没——
        请先 UNSUBSCRIBE 再用本方法（05 示例有演示）。
        """
        self.ser.reset_input_buffer()
        self.ser.write((line.rstrip() + "\n").encode("ascii"))
        out = bytearray()
        deadline = time.time() + max_wait
        last = time.time()
        while time.time() < deadline and time.time() - last < idle:
            chunk = self.ser.read(64)
            if chunk:
                out += chunk
                last = time.time()
                if out.rstrip(b" \r\n").endswith(b">"):
                    break
        return out.decode("utf-8", errors="replace").strip()

    # ---------- 二进制帧（§5.3~§5.6） ----------

    def send_frame(self, frame):
        self.ser.write(bytes(frame))

    def read_frame(self, timeout=None):
        """读取一帧有效帧（自动丢弃噪声/CRC 错帧）。返回 (cmd, data) 或 None。"""
        timeout = self.timeout if timeout is None else timeout
        deadline = time.time() + timeout
        while time.time() < deadline:
            cmd, data, used = parse_stream(self._rxbuf)
            if cmd is None:
                if used:
                    del self._rxbuf[:used]
            else:
                del self._rxbuf[:used]
                return cmd, data
            chunk = self.ser.read(1)
            if chunk:
                self._rxbuf += chunk
        return None

    def request(self, cmd, data=b""):
        """发送一帧并等待同命令字的 ACK/应答。返回应答 data；超时返回 None。"""
        self.send_frame(build_frame(cmd, data))
        deadline = time.time() + self.timeout
        while time.time() < deadline:
            r = self.read_frame(timeout=deadline - time.time())
            if r is None:
                return None
            rcmd, rdata = r
            if rcmd == cmd:
                return rdata
            # 非本命令的帧（如 0xF0 推送）：丢弃继续等
        return None

    # ---------- 常用封装 ----------

    def ping(self):
        """PING (0x01)。成功返回 True。"""
        return self.request(CMD_PING) == b"\x00"

    def read_param(self):
        """READ_PARAM (0x10)：返回 248B config_t（§5.7）。"""
        d = self.request(CMD_READ_PARAM)
        if d is None or len(d) != CONFIG_SIZE:
            raise IOError("READ_PARAM 失败（长度 %s，期望 %d）"
                          % (None if d is None else len(d), CONFIG_SIZE))
        return d

    def write_field(self, offset, value):
        """WRITE_FIELD (0x12)：按偏移写单字段。value 为小端字节串。"""
        r = self.request(CMD_WRITE_FIELD, struct.pack("<H", offset) + bytes(value))
        if r is None:
            raise IOError("WRITE_FIELD 无响应")
        if r == b"\x02":
            raise IOError("WRITE_FIELD 被拒：offset %d 在受保护区 (<11)（§5.4/§5.7）" % offset)
        if r != b"\x00":
            raise IOError("WRITE_FIELD 失败（err=%s）" % r.hex())
        return True

    def save(self):
        """SAVE_EEPROM (0x20)：RAM 配置持久化（约 190ms）。"""
        r = self.request(CMD_SAVE_EEPROM)
        if r != b"\x00":
            raise IOError("SAVE_EEPROM 失败（err=%s）" % ("超时" if r is None else r.hex()))
        return True

    def motor_ctrl(self, p1, p2, p3, p4=0):
        """
        MOTOR_CTRL (0x31)（§5.5）：
        单电机模式：p1~p4 = 通道 A~D 各自目标（开环=PWM ±1000 / 速度=RPM / 位置=0.1°）
        底盘模式： p1=vx(mm/s) p2=vy(mm/s) p3=ω(0.001rad/s) p4=保留
        本命令无 ACK。
        """
        self.send_frame(build_frame(CMD_MOTOR_CTRL, struct.pack("<4i", p1, p2, p3, p4)))

    def subscribe(self, interval_ms=50):
        """SUBSCRIBE (0x40)：开启 STATUS_REPORT 周期推送（最低 20ms）。"""
        r = self.request(CMD_SUBSCRIBE, struct.pack("<H", interval_ms))
        if r != b"\x00":
            raise IOError("SUBSCRIBE 失败")
        return True

    def unsubscribe(self):
        return self.request(CMD_UNSUBSCRIBE) == b"\x00"


def parse_status(payload):
    """
    解析 STATUS_REPORT (0xF0) payload（§5.6）。
    返回 dict：enc/tgt/rpm（及扩展模式下的 rpm_raw）。
    """
    if len(payload) == 56:
        enc = struct.unpack("<4i", payload[0:16])
        tgt = struct.unpack("<4f", payload[16:32])
        rpm = struct.unpack("<4i", payload[32:48])
        fcnt, ocnt = struct.unpack("<II", payload[48:56])
        return {"enc": enc, "tgt": tgt, "rpm": rpm,
                "sbus_frame_cnt": fcnt, "sbus_ok_cnt": ocnt, "raw": False}
    if len(payload) == 72:
        enc = struct.unpack("<4i", payload[0:16])
        tgt = struct.unpack("<4f", payload[16:32])
        rpm = struct.unpack("<4i", payload[32:48])
        raw = struct.unpack("<4i", payload[48:64])
        fcnt, ocnt = struct.unpack("<II", payload[64:72])
        return {"enc": enc, "tgt": tgt, "rpm": rpm, "rpm_raw": raw,
                "sbus_frame_cnt": fcnt, "sbus_ok_cnt": ocnt, "raw": True}
    raise ValueError("STATUS_REPORT payload 长度异常: %d" % len(payload))
