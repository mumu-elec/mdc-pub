# -*- coding: utf-8 -*-
"""
motor_driver.py — Motor Driver Controller 二进制帧协议封装（MicroPython）

适用场景：MicroPython 板（以 ESP32 为例）经 UART2 连接控制板 RC 口（USART2）。
协议细节以《协议规范.md》为准：
  - 帧格式:   [0xAA][CMD][LEN][DATA...][CRC8]
  - CRC8:     多项式 0x07，初值 0，计算范围 = CMD + LEN + DATA（不含 SYNC）
  - 多字节字段: 小端序（LE）
  - ACK:      0xAA + CMD + 0x01 + err(0x00 成功 / 0xFF 失败) + CRC8

仅依赖 MicroPython 标准库：machine / utime / ustruct。
在 CPython 上可用 python -m py_compile 做语法检查（import 不影响语法检查）。
"""

from machine import UART, Pin
import utime
import ustruct

# ---------------- 用户可调参数（按板子修改） ----------------
UART_ID = 2            # ESP32 UART 编号
TX_PIN = 17            # ESP32 TX -> 控制板 RC 信号（USART2 RX）
RX_PIN = 16            # ESP32 RX <- 控制板 RC 信号（USART2 TX）
BAUD = 115200          # 波特率，须与控制板 /uart2 配置一致
TIMEOUT_MS = 100       # 默认读帧/ACK 超时（ms）
RXBUF = 1024           # UART 接收缓冲区，须 >= 231B（config_t 大小）
# ------------------------------------------------------------

SYNC = 0xAA            # 帧同步字
CRC8_POLY = 0x07       # CRC8 多项式

# ---- 命令字（协议规范 §3.3，共 18 条）----
CMD_PING          = 0x01
CMD_READ_PARAM    = 0x10
CMD_WRITE_PARAM   = 0x11
CMD_WRITE_FIELD   = 0x12
CMD_SAVE_EEPROM   = 0x20
CMD_LOAD_EEPROM   = 0x21
CMD_FACTORY_RESET = 0x22
CMD_MOTOR_RAW     = 0x30
CMD_MOTOR_CTRL    = 0x31
CMD_SUBSCRIBE     = 0x40
CMD_UNSUBSCRIBE   = 0x41
CMD_DEBUG_SBUS    = 0x43
CMD_DEBUG_SPEED   = 0x44
CMD_ENTER_BL      = 0x52
CMD_REBOOT        = 0x53
CMD_STATUS_REPORT = 0xF0
CMD_DETECT_REPORT = 0xF1
CMD_SBUS_DATA     = 0xF2

CONFIG_T_SIZE = 231   # config_t 大小（布局 v2.1）


def crc8(data):
    """CRC8 校验（多项式 0x07，初值 0，按位计算）。返回 0~255 的整数。

    与协议规范 §3.1 的 C 参考实现逐位等价。
    """
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            if c & 0x80:
                c = ((c << 1) ^ CRC8_POLY) & 0xFF
            else:
                c = (c << 1) & 0xFF
    return c


def build_frame(cmd, data=b""):
    """组帧: [0xAA][CMD][LEN][DATA...][CRC8(CMD+LEN+DATA)] -> bytes。"""
    if len(data) > 250:
        raise ValueError("DATA 长度超过 250B")
    body = bytes((cmd, len(data))) + bytes(data)
    return bytes((SYNC,)) + body + bytes((crc8(body),))


def parse_status_report(payload):
    """解析 0xF0 STATUS_REPORT 数据段（协议规范 §4），按长度兼容两种模式。

    常规模式 56B: enc[4]i32 + tgt[4]f32 + rpm[4]i32 + sbus_frame_cnt u32 + sbus_ok_cnt u32
    扩展模式 72B: 上述基础上追加 rpm_raw[4]i32（需先经 0x44 DEBUG_SPEED 开启）

    返回 dict: enc / tgt / rpm / rpm_raw(扩展模式，常规为 None) /
               sbus_frame_cnt / sbus_ok_cnt
    """
    n = len(payload)
    if n == 56:
        enc = list(ustruct.unpack("<4i", payload[0:16]))
        tgt = list(ustruct.unpack("<4f", payload[16:32]))
        rpm = list(ustruct.unpack("<4i", payload[32:48]))
        frame_cnt, ok_cnt = ustruct.unpack("<II", payload[48:56])
        rpm_raw = None
    elif n == 72:
        enc = list(ustruct.unpack("<4i", payload[0:16]))
        tgt = list(ustruct.unpack("<4f", payload[16:32]))
        rpm = list(ustruct.unpack("<4i", payload[32:48]))
        rpm_raw = list(ustruct.unpack("<4i", payload[48:64]))
        frame_cnt, ok_cnt = ustruct.unpack("<II", payload[64:72])
    else:
        raise ValueError("未知 STATUS_REPORT 长度: %d (应为 56 或 72)" % n)
    return {
        "enc": enc,
        "tgt": tgt,
        "rpm": rpm,
        "rpm_raw": rpm_raw,
        "sbus_frame_cnt": frame_cnt,
        "sbus_ok_cnt": ok_cnt,
    }


class MotorDriver:
    """控制板二进制协议客户端。"""

    def __init__(self, uart_id=UART_ID, tx=TX_PIN, rx=RX_PIN, baud=BAUD,
                 timeout_ms=TIMEOUT_MS, rxbuf=RXBUF):
        self.timeout_ms = timeout_ms
        self._rx = b""  # 跨次调用保留的未解析接收缓冲
        self.uart = UART(uart_id, baudrate=baud, tx=Pin(tx), rx=Pin(rx),
                         rxbuf=rxbuf, timeout=timeout_ms)

    # ---------------- 底层收发 ----------------
    def send_frame(self, cmd, data=b""):
        """发送一帧二进制指令（组帧后写入 UART）。"""
        self.uart.write(build_frame(cmd, data))

    def read_frame(self, timeout_ms=None):
        """读取并解析一帧二进制数据，返回 (cmd, payload)；超时返回 None。

        实现：滑动窗口搜索 0xAA -> 按 LEN 收满整帧 -> CRC8 校验通过才返回；
        CRC 失败则丢弃当前 SYNC 继续向后搜索，可容忍串口上的杂散字节。
        未解析完的字节保留在内部缓冲，供下次调用继续（适配连续上报流）。
        """
        if timeout_ms is None:
            timeout_ms = self.timeout_ms
        buf = self._rx
        self._rx = b""
        deadline = utime.ticks_ms() + timeout_ms
        while utime.ticks_diff(deadline, utime.ticks_ms()) > 0:
            n = self.uart.any()
            if n:
                buf += self.uart.read(n)
            # ---- 滑动窗口解析 ----
            while True:
                idx = -1
                for i in range(len(buf)):
                    if buf[i] == SYNC:
                        idx = i
                        break
                if idx < 0:                 # 缓冲中无 SYNC：清空等待新数据
                    buf = b""
                    break
                if idx > 0:                 # 丢弃 SYNC 前的杂散字节
                    buf = buf[idx:]
                if len(buf) < 3:            # 至少需要 CMD + LEN 两个字节
                    break
                plen = buf[2]               # DATA 段长度
                if len(buf) < 4 + plen:     # 整帧(含CRC)未收完：继续等待
                    break
                frame = buf[:4 + plen]      # SYNC+CMD+LEN+DATA+CRC 共 4+plen 字节
                buf = buf[4 + plen:]
                body = frame[1:3 + plen]    # CMD + LEN + DATA（CRC 计算范围）
                if frame[-1] == crc8(body):
                    self._rx = buf          # 保留可能存在的下一条帧
                    return frame[1], bytes(frame[3:3 + plen])
                # CRC 校验失败：丢弃当前 SYNC，继续向后找下一个
            if not n:
                utime.sleep_ms(2)
        self._rx = buf
        return None

    def read_ack(self, cmd, timeout_ms=None):
        """读取并校验 ACK 帧。

        返回：True = 成功（err=0x00）；False = 失败（err=0xFF）；None = 超时。
        """
        res = self.read_frame(timeout_ms)
        if res is None:
            return None
        rcmd, payload = res
        if rcmd != cmd:
            raise ValueError("ACK 命令字不匹配: 期望 0x%02X, 收到 0x%02X" % (cmd, rcmd))
        if len(payload) != 1:
            raise ValueError("ACK 载荷长度错误: %d" % len(payload))
        return payload[0] == 0x00

    # ---------------- 常用命令封装 ----------------
    def ping(self, timeout_ms=None):
        """0x01 PING：连通性测试，返回 ACK 结果（True/False/None）。"""
        self.send_frame(CMD_PING)
        return self.read_ack(CMD_PING, timeout_ms)

    def read_param(self, timeout_ms=1000):
        """0x10 READ_PARAM：读取全部配置，返回 config_t（231B bytes）。"""
        self.send_frame(CMD_READ_PARAM)
        res = self.read_frame(timeout_ms)
        if res is None:
            raise OSError("READ_PARAM 超时")
        rcmd, payload = res
        if rcmd != CMD_READ_PARAM:
            raise ValueError("应答命令字错误: 0x%02X" % rcmd)
        return payload

    def save(self, timeout_ms=1500):
        """0x20 SAVE_EEPROM：RAM 配置写入 EEPROM（约 190ms）。"""
        self.send_frame(CMD_SAVE_EEPROM)
        return self.read_ack(CMD_SAVE_EEPROM, timeout_ms)

    def motor_ctrl(self, targets):
        """0x31 MOTOR_CTRL：四通道批量控制。targets 为 4 个 int（int32 LE）。

        目标值含义随各通道控制模式：开环 = PWM（±1000）、速度 = RPM、位置 = 0.1°。
        实时控制需以 30/50/100ms 周期连续发送（受优先级仲裁，USART2 默认优先）。
        """
        if len(targets) != 4:
            raise ValueError("MOTOR_CTRL 需要 4 个目标值")
        data = ustruct.pack("<4i", targets[0], targets[1], targets[2], targets[3])
        self.send_frame(CMD_MOTOR_CTRL, data)

    def subscribe(self, interval_ms, timeout_ms=None):
        """0x40 SUBSCRIBE：开启状态周期上报（interval_ms >= 20ms）。"""
        if interval_ms < 20:
            raise ValueError("SUBSCRIBE 最小周期 20ms")
        self.send_frame(CMD_SUBSCRIBE, ustruct.pack("<H", interval_ms))
        return self.read_ack(CMD_SUBSCRIBE, timeout_ms)

    def unsubscribe(self, timeout_ms=None):
        """0x41 UNSUBSCRIBE：关闭状态上报。"""
        self.send_frame(CMD_UNSUBSCRIBE)
        return self.read_ack(CMD_UNSUBSCRIBE, timeout_ms)

    def debug_speed(self, enable, timeout_ms=None):
        """0x44 DEBUG_SPEED：状态上报扩展模式开关（开启后 0xF0 为 72B）。"""
        self.send_frame(CMD_DEBUG_SPEED, b"\x01" if enable else b"\x00")
        return self.read_ack(CMD_DEBUG_SPEED, timeout_ms)
