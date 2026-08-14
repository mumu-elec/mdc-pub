#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
motor_driver.py — Motor Driver Controller 二进制帧协议封装库
==============================================================
帧格式（协议规范 §3）：
    [SYNC=0xAA] [CMD:1B] [LEN:1B] [DATA:0~250B] [CRC8:1B]
    * CRC8：多项式 0x07，初值 0，计算范围 = CMD + LEN + DATA（不含 SYNC）
    * 所有多字节字段为小端序（LE）
    * ACK 帧：0xAA + CMD + 0x01 + err(0x00=成功/0xFF=失败) + CRC8

命令表（18 条）见协议规范 §3.3；config_t（231B）布局见 §5。

用法示例：
    from motor_driver import MotorDriver, CMD_PING
    drv = MotorDriver("COM5")
    err = drv.ping()                 # 0 = 成功
    cfg = drv.read_param()           # 231B config_t
    drv.motor_ctrl([300, 0, 0, 0])   # 通道1 开环 PWM 300
    drv.close()
"""

import struct
import time

import serial

# ── 帧常量 ────────────────────────────────────────────────
SYNC = 0xAA              # 帧同步字节
MAX_DATA_LEN = 250       # DATA 段最大长度（config_t = 231）
CONFIG_SIZE = 231        # config_t 大小

# ── 命令码（与固件 binary_proto.h 一致，协议规范 §3.3）────
CMD_PING           = 0x01   # 连通性测试
CMD_READ_PARAM     = 0x10   # 读取全部配置（应答 config_t 231B）
CMD_WRITE_PARAM    = 0x11   # 写入全部配置（仅 RAM）
CMD_WRITE_FIELD    = 0x12   # 按偏移写入单个字段
CMD_SAVE_EEPROM    = 0x20   # RAM → EEPROM
CMD_LOAD_EEPROM    = 0x21   # EEPROM → RAM
CMD_FACTORY_RESET  = 0x22   # 恢复出厂默认
CMD_MOTOR_RAW      = 0x30   # 单通道 PWM 直驱
CMD_MOTOR_CTRL     = 0x31   # 四通道批量控制（核心控制帧）
CMD_SUBSCRIBE      = 0x40   # 开启状态周期上报
CMD_UNSUBSCRIBE    = 0x41   # 关闭状态上报
CMD_DEBUG_SBUS     = 0x43   # SBUS 通道上报开关
CMD_DEBUG_SPEED    = 0x44   # 速度原始值上报开关（0xF0 扩展）
CMD_ENTER_BL       = 0x52   # 软复位进 Bootloader
CMD_REBOOT         = 0x53   # 系统重启
CMD_STATUS_REPORT  = 0xF0   # 状态上报（MCU 主动推送）
CMD_DETECT_REPORT  = 0xF1   # 协议检测结果上报
CMD_SBUS_DATA      = 0xF2   # SBUS 16 通道原始值上报

# ── ACK 错误码 ────────────────────────────────────────────
ACK_OK = 0x00
ACK_ERR = 0xFF


class ProtocolError(Exception):
    """协议层错误（超时 / ACK 失败 / 参数非法等）。"""


class MotorDriver:
    """Motor Driver Controller 二进制协议封装。

    线程安全说明：本类供单线程使用；订阅（subscribe）后 MCU 会持续推送
    0xF0 帧，等待 ACK / 配置应答期间这些帧会被 read_frame 自动跳过。
    """

    # ── CRC8 / 组帧（静态方法）────────────────────────────
    @staticmethod
    def crc8(data):
        """CRC8-ATM：多项式 0x07，初值 0（协议规范 §3.1 位循环算法移植）。

        注意：计算范围是 CMD+LEN+DATA，不含 SYNC。
        """
        c = 0
        for b in data:
            c ^= b
            for _ in range(8):
                if c & 0x80:
                    c = ((c << 1) ^ 0x07) & 0xFF
                else:
                    c = (c << 1) & 0xFF
        return c

    @staticmethod
    def build_frame(cmd, data=b""):
        """组帧：[0xAA, cmd, len, data..., crc8(cmd+len+data)] -> bytes"""
        if len(data) > MAX_DATA_LEN:
            raise ValueError(f"DATA 长度 {len(data)} 超过上限 {MAX_DATA_LEN}")
        body = bytes([cmd, len(data)]) + data
        return bytes([SYNC]) + body + bytes([MotorDriver.crc8(body)])

    # ── 连接管理 ──────────────────────────────────────────
    def __init__(self, port, baudrate=2000000):
        self.port = port
        self.baudrate = baudrate
        # 串口用短超时：帧解析由 read_frame 的滑动窗口 + 截止时间自管理
        self.ser = serial.Serial(port, baudrate, timeout=0.05)
        self.ser.reset_input_buffer()   # 清空历史残留字节
        self._rx = b""                  # 接收缓冲（帧解析滑动窗口）

    def close(self):
        if self.ser is not None and self.ser.is_open:
            self.ser.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    # ── 底层收发 ──────────────────────────────────────────
    def send_frame(self, cmd, data=b""):
        """发送一帧二进制数据。"""
        self.ser.write(self.build_frame(cmd, data))

    def _extract_frame(self):
        """从接收缓冲中提取一帧；无完整帧返回 None。

        滑动窗口：查找 0xAA 同步字节 → 读 CMD/LEN → 按 LEN 收完 DATA+CRC →
        CRC 校验；LEN 非法或 CRC 失败则丢弃该 0xAA 继续向后找。
        """
        buf = self._rx
        while True:
            idx = buf.find(bytes([SYNC]))
            if idx < 0:
                self._rx = b""
                return None
            if idx > 0:
                buf = buf[idx:]                 # 丢弃 SYNC 前的垃圾字节
            if len(buf) < 3:                    # 至少需要 CMD+LEN
                self._rx = buf
                return None
            cmd = buf[1]
            ln = buf[2]
            if ln > MAX_DATA_LEN:               # LEN 非法（DATA 最大 250）
                buf = buf[1:]
                continue
            fsize = 3 + ln + 1                  # CMD+LEN+DATA+CRC
            if len(buf) < fsize:
                self._rx = buf                  # 数据未收全，等待更多字节
                return None
            if MotorDriver.crc8(buf[1:fsize - 1]) == buf[fsize - 1]:
                self._rx = buf[fsize:]          # CRC 通过
                return (cmd, buf[3:fsize - 1])
            buf = buf[1:]                       # CRC 失败：跳过该 0xAA 继续找

    def read_frame(self, timeout=1.0):
        """读取并解析一帧，返回 (cmd, data)；超时返回 None。"""
        deadline = time.monotonic() + timeout
        while True:
            frame = self._extract_frame()
            if frame is not None:
                return frame
            remain = deadline - time.monotonic()
            if remain <= 0:
                return None
            chunk = self.ser.read(self.ser.in_waiting or 1)
            if chunk:
                self._rx += chunk

    def read_ack(self, cmd, timeout=0.5):
        """等待指定命令的 ACK 帧并校验。

        返回：ACK 的 err 字节（int）。
            err == 0x00 → 成功；err == 0xFF → 失败；
            固件特定场景可能返回 0x01（LEN 不匹配）/ 0x02（受保护字段）。
        超时返回 None。
        等待期间自动跳过其它帧（如订阅开启后的 STATUS_REPORT 0xF0 帧）。
        """
        deadline = time.monotonic() + timeout
        while True:
            remain = deadline - time.monotonic()
            if remain <= 0:
                return None
            frame = self.read_frame(timeout=max(0.01, remain))
            if frame is None:
                return None
            c, d = frame
            if c == cmd and len(d) == 1:
                return d[0]

    def wait_ack_ok(self, cmd, timeout=0.5):
        """read_ack 的便捷封装：ACK 成功返回 True；失败/超时抛 ProtocolError。"""
        err = self.read_ack(cmd, timeout)
        if err is None:
            raise ProtocolError(f"等待 ACK 超时 (cmd=0x{cmd:02X})")
        if err != ACK_OK:
            raise ProtocolError(f"ACK 失败 (cmd=0x{cmd:02X}, err=0x{err:02X})")
        return True

    # ── 系统命令 ──────────────────────────────────────────
    def ping(self, timeout=0.5):
        """PING（0x01）：连通性测试，返回 ACK 错误码（0=成功，None=超时）。"""
        self.send_frame(CMD_PING)
        return self.read_ack(CMD_PING, timeout)

    def read_param(self, timeout=1.0):
        """READ_PARAM（0x10）：读取 config_t，返回 231B bytes。"""
        self.send_frame(CMD_READ_PARAM)
        deadline = time.monotonic() + timeout
        while True:
            remain = deadline - time.monotonic()
            if remain <= 0:
                raise ProtocolError("READ_PARAM 超时")
            frame = self.read_frame(timeout=max(0.01, remain))
            if frame is None:
                raise ProtocolError("READ_PARAM 超时")
            c, d = frame
            if c == CMD_READ_PARAM and len(d) == CONFIG_SIZE:
                return d
            # 其它帧（如 STATUS_REPORT）跳过，继续等待配置数据

    def write_param(self, data, timeout=0.5):
        """WRITE_PARAM（0x11）：写入 config_t（仅 RAM，受保护字段自动还原）。"""
        if len(data) != CONFIG_SIZE:
            raise ValueError(f"WRITE_PARAM 需要 {CONFIG_SIZE}B 数据，实际 {len(data)}B")
        self.send_frame(CMD_WRITE_PARAM, data)
        return self.wait_ack_ok(CMD_WRITE_PARAM, timeout)

    def write_field(self, field_id, value, timeout=0.5):
        """WRITE_FIELD（0x12）：按偏移量写入单个字段。

        field_id 为 config_t 字节偏移（LE u16）；value 为按字段类型打包好的 bytes。
        规范：offset < 12（受保护区 magic/hw_ver/sw_ver/reserved/crc）拒绝写入。
        （固件实际拒绝 offset < 11，规范表述为 <12，本库按规范执行。）
        """
        if field_id < 12:
            raise ProtocolError(
                f"offset {field_id} 位于受保护区（协议规范：offset<12 拒绝写入）")
        self.send_frame(CMD_WRITE_FIELD, struct.pack("<H", field_id) + value)
        return self.wait_ack_ok(CMD_WRITE_FIELD, timeout)

    def save(self, timeout=1.0):
        """SAVE_EEPROM（0x20）：RAM 配置写入 EEPROM（固件耗时约 190ms）。"""
        self.send_frame(CMD_SAVE_EEPROM)
        return self.wait_ack_ok(CMD_SAVE_EEPROM, timeout)

    def load(self, timeout=1.0):
        """LOAD_EEPROM（0x21）：从 EEPROM 重新加载配置到 RAM。"""
        self.send_frame(CMD_LOAD_EEPROM)
        return self.wait_ack_ok(CMD_LOAD_EEPROM, timeout)

    def factory_reset(self, timeout=1.0):
        """FACTORY_RESET（0x22）：恢复出厂默认（RAM + EEPROM）。"""
        self.send_frame(CMD_FACTORY_RESET)
        return self.wait_ack_ok(CMD_FACTORY_RESET, timeout)

    # ── 电机控制 ──────────────────────────────────────────
    def motor_raw(self, ch, direction, pwm, timeout=0.1):
        """MOTOR_RAW（0x30）：单通道 PWM 直驱（旁路 PID）。

        ch=0~3（对应电机 A~D）；direction=0 正转 / 1 反转（内部取负）；
        pwm=0~1000。受优先级仲裁（需 /priority 1 且未处于协议识别期）。
        """
        if not 0 <= ch <= 3:
            raise ValueError(f"ch 取值 0~3，实际 {ch}")
        if direction not in (0, 1):
            raise ValueError(f"direction 取值 0/1，实际 {direction}")
        if not 0 <= pwm <= 1000:
            raise ValueError(f"pwm 取值 0~1000，实际 {pwm}")
        self.send_frame(CMD_MOTOR_RAW, bytes([ch, direction]) + struct.pack("<H", pwm))

    def motor_ctrl(self, targets, timeout=0.1):
        """MOTOR_CTRL（0x31）：四通道批量控制帧（核心控制帧）。

        targets：长度 4 的 int 列表，int32 LE 打包。
        含义随各通道控制模式：开环 = PWM（±1000）；速度 = RPM；
        位置 = 0.1°（±3600 = ±360.0°）。
        实时控制应以 30/50/100ms 间隔连续发送本帧。受优先级仲裁。
        """
        if len(targets) != 4:
            raise ValueError(f"MOTOR_CTRL 需要 4 个目标值，实际 {len(targets)}")
        self.send_frame(CMD_MOTOR_CTRL, struct.pack("<4i", *[int(v) for v in targets]))

    # ── 订阅 / 调试 ───────────────────────────────────────
    def subscribe(self, interval_ms, timeout=0.5):
        """SUBSCRIBE（0x40）：开启 STATUS_REPORT（0xF0）周期上报。

        固件最低上报周期 20ms。
        """
        if interval_ms < 20:
            raise ValueError(f"上报周期最低 20ms（固件限制），实际 {interval_ms}ms")
        self.send_frame(CMD_SUBSCRIBE, struct.pack("<H", int(interval_ms)))
        return self.wait_ack_ok(CMD_SUBSCRIBE, timeout)

    def unsubscribe(self, timeout=0.5):
        """UNSUBSCRIBE（0x41）：关闭状态上报。"""
        self.send_frame(CMD_UNSUBSCRIBE)
        return self.wait_ack_ok(CMD_UNSUBSCRIBE, timeout)

    def debug_sbus(self, enable, timeout=0.5):
        """DEBUG_SBUS（0x43）：SBUS 16 通道实时上报（0xF2）开关。"""
        self.send_frame(CMD_DEBUG_SBUS, bytes([1 if enable else 0]))
        return self.wait_ack_ok(CMD_DEBUG_SBUS, timeout)

    def debug_speed(self, enable, timeout=0.5):
        """DEBUG_SPEED（0x44）：速度原始值上报开关。

        开启后 STATUS_REPORT 由 56B 扩展为 72B（追加 rpm_raw[4]）。
        """
        self.send_frame(CMD_DEBUG_SPEED, bytes([1 if enable else 0]))
        return self.wait_ack_ok(CMD_DEBUG_SPEED, timeout)

    def reboot(self, timeout=0.3):
        """REBOOT（0x53）：系统重启（固件 ACK 后延迟 100ms 复位）。

        设备复位后可能来不及回 ACK，故 ACK 丢失不视为错误。
        """
        self.send_frame(CMD_REBOOT)
        try:
            return self.wait_ack_ok(CMD_REBOOT, timeout)
        except ProtocolError:
            return False

    def enter_bl(self, timeout=0.5):
        """ENTER_BL（0x52）：软复位进入 Bootloader（写 RTC magic + SystemReset）。"""
        self.send_frame(CMD_ENTER_BL)
        return self.wait_ack_ok(CMD_ENTER_BL, timeout)

    # ── 状态帧解析（订阅场景）─────────────────────────────
    @staticmethod
    def parse_status_report(payload):
        """解析 STATUS_REPORT（0xF0）payload（全部小端）。

        常规 56B：enc[4]i32 + tgt[4]f32 + rpm[4]i32 + sbus_frame_cnt:u32 + sbus_ok_cnt:u32
        扩展 72B：上述基础上追加 rpm_raw[4]i32（需先 DEBUG_SPEED=1）

        返回 dict；未知长度抛 ProtocolError。
        """
        if len(payload) == 56:
            enc = list(struct.unpack("<4i", payload[0:16]))
            tgt = list(struct.unpack("<4f", payload[16:32]))
            rpm = list(struct.unpack("<4i", payload[32:48]))
            fc, oc = struct.unpack("<II", payload[48:56])
            return {"enc": enc, "tgt": tgt, "rpm": rpm,
                    "rpm_raw": None, "sbus_frame_cnt": fc, "sbus_ok_cnt": oc}
        if len(payload) == 72:
            enc = list(struct.unpack("<4i", payload[0:16]))
            tgt = list(struct.unpack("<4f", payload[16:32]))
            rpm = list(struct.unpack("<4i", payload[32:48]))
            raw = list(struct.unpack("<4i", payload[48:64]))
            fc, oc = struct.unpack("<II", payload[64:72])
            return {"enc": enc, "tgt": tgt, "rpm": rpm,
                    "rpm_raw": raw, "sbus_frame_cnt": fc, "sbus_ok_cnt": oc}
        raise ProtocolError(f"STATUS_REPORT payload 长度 {len(payload)} 非法（应为 56 或 72）")


# 模块级便捷别名（也可独立使用）
crc8 = MotorDriver.crc8
build_frame = MotorDriver.build_frame
