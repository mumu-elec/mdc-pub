#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
05_status_monitor — 状态监控例程
=================================
订阅 0xF0 STATUS_REPORT 周期上报（0x40 SUBSCRIBE），后台读线程解析帧，
控制台实时表格刷新（\r 覆盖 / ANSI 光标回退），可选 --csv 每帧记录一行。

STATUS_REPORT 两种 payload（协议规范 §4，全部小端）：
    常规 56B: enc[4]i32 + tgt[4]f32 + rpm[4]i32 + sbus_frame_cnt:u32 + sbus_ok_cnt:u32
    扩展 72B: 上述 + rpm_raw[4]i32（需先 DEBUG_SPEED=1）
本工具按 payload 长度自动兼容两种格式。

用法：
    python status_monitor.py                 # 自动选择第一个 CH340, 50ms 周期
    python status_monitor.py --port COM5 --interval 20
    python status_monitor.py --csv log.csv   # 同时记录 CSV

Ctrl+C 干净退出（自动发送 0x41 UNSUBSCRIBE）。
"""

import argparse
import csv
import os
import struct
import sys
import threading
import time

import serial

BAUDRATE = 2000000


def setup_console():
    """控制台输出统一 UTF-8（兼容 GBK / 输出重定向场景）。"""
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass

# 二进制帧常量（协议规范 §3）
SYNC = 0xAA
CMD_SUBSCRIBE = 0x40
CMD_UNSUBSCRIBE = 0x41
CMD_STATUS_REPORT = 0xF0
ACK_OK = 0x00


def crc8(data):
    """CRC8-ATM：多项式 0x07，初值 0，范围 CMD+LEN+DATA（不含 SYNC）。"""
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if (c & 0x80) else (c << 1) & 0xFF
    return c


def build_frame(cmd, data=b""):
    """组帧：[0xAA, cmd, len, data..., crc8]"""
    body = bytes([cmd, len(data)]) + data
    return bytes([SYNC]) + body + bytes([crc8(body)])


def parse_status(payload):
    """解析 0xF0 payload，按长度自动区分常规 56B / 扩展 72B。返回 dict。"""
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
    raise ValueError(f"STATUS_REPORT payload 长度 {len(payload)} 非法（应为 56 或 72）")


def enable_vt():
    """Windows 控制台启用 ANSI 转义（ENABLE_VIRTUAL_TERMINAL_PROCESSING）。"""
    if os.name != "nt":
        return True
    try:
        import ctypes
        kernel32 = ctypes.windll.kernel32
        handle = kernel32.GetStdHandle(-11)      # STD_OUTPUT_HANDLE
        mode = ctypes.c_uint32()
        if kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
            kernel32.SetConsoleMode(handle, mode.value | 0x0004)
            return True
    except Exception:
        pass
    return False


class StatusMonitor:
    """订阅状态上报：后台线程解析 + 主线程表格刷新。"""

    def __init__(self, port, interval_ms, csv_path=None):
        if interval_ms < 20:
            raise ValueError(f"固件最低上报周期 20ms，实际 {interval_ms}ms")
        self.interval_ms = interval_ms
        self.ser = serial.Serial(port, BAUDRATE, timeout=0.05)
        self.ser.reset_input_buffer()
        self._rx = b""                 # 帧解析滑动窗口
        self._latest = None            # 最近一帧解析结果
        self._stop = threading.Event()
        self._frames = 0

        # CSV（可选）
        self._csv_fp = None
        self._csv_writer = None
        if csv_path:
            self._csv_fp = open(csv_path, "w", newline="", encoding="utf-8")
            self._csv_writer = csv.writer(self._csv_fp)
            self._csv_writer.writerow(
                ["t_s", "enc1", "enc2", "enc3", "enc4",
                 "tgt1", "tgt2", "tgt3", "tgt4",
                 "rpm1", "rpm2", "rpm3", "rpm4",
                 "rpm_raw1", "rpm_raw2", "rpm_raw3", "rpm_raw4",
                 "sbus_frame_cnt", "sbus_ok_cnt"])
            self._csv_fp.flush()

        # 开启订阅（同步等 ACK，确保固件已开始推送再起读线程）
        self.ser.write(build_frame(CMD_SUBSCRIBE, struct.pack("<H", interval_ms)))
        if self._wait_ack(CMD_SUBSCRIBE) != ACK_OK:
            raise RuntimeError("SUBSCRIBE ACK 失败或超时")

        self._t0 = time.monotonic()
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    # ── 帧解析 ────────────────────────────────────────────
    def _extract(self):
        buf = self._rx
        while True:
            idx = buf.find(bytes([SYNC]))
            if idx < 0:
                self._rx = b""
                return None
            if idx > 0:
                buf = buf[idx:]
            if len(buf) < 3:
                self._rx = buf
                return None
            ln = buf[2]
            if ln > 250:
                buf = buf[1:]
                continue
            fsize = 3 + ln + 1
            if len(buf) < fsize:
                self._rx = buf
                return None
            if crc8(buf[1:fsize - 1]) == buf[fsize - 1]:
                self._rx = buf[fsize:]
                return (buf[1], buf[3:fsize - 1])
            buf = buf[1:]

    def _wait_ack(self, cmd, timeout=0.5):
        """等待指定命令的 ACK（跳过其它帧）。返回 err 字节或 None。"""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            frame = self._extract()
            if frame is None:
                remain = deadline - time.monotonic()
                if remain <= 0:
                    return None
                chunk = self.ser.read(self.ser.in_waiting or 1)
                if chunk:
                    self._rx += chunk
                continue
            c, d = frame
            if c == cmd and len(d) == 1:
                return d[0]
        return None

    # ── 后台读线程 ────────────────────────────────────────
    def _reader(self):
        while not self._stop.is_set():
            frame = None
            # 先尝试从缓冲提取
            frame = self._extract()
            if frame is None:
                chunk = self.ser.read(4096)     # 阻塞 ≤ 串口 timeout(0.05s)
                if chunk:
                    self._rx += chunk
                    frame = self._extract()
            if frame is None:
                continue
            c, d = frame
            if c == CMD_STATUS_REPORT:
                try:
                    st = parse_status(d)
                except ValueError:
                    continue
                st["t_s"] = time.monotonic() - self._t0
                self._latest = st
                self._frames += 1
                if self._csv_writer is not None:
                    raw = st["rpm_raw"] or [""] * 4
                    self._csv_writer.writerow(
                        [f"{st['t_s']:.3f}"] + st["enc"] +
                        [f"{v:.1f}" for v in st["tgt"]] + st["rpm"] +
                        list(raw) + [st["sbus_frame_cnt"], st["sbus_ok_cnt"]])
                    self._csv_fp.flush()

    # ── 显示 ──────────────────────────────────────────────
    def _render_table(self, st):
        if st is None:
            return ["(等待第一帧 STATUS_REPORT...)"] * 6
        lines = ["  CH |    enc(脉冲)   |    tgt(当前)   |   rpm(滤波)   | rpm_raw(滤波前)"]
        lines.append("-----+----------------+----------------+---------------+----------------")
        for i in range(4):
            raw = st["rpm_raw"][i] if st["rpm_raw"] else "-"
            lines.append(
                f"  {i + 1}  | {st['enc'][i]:>12d}  | {st['tgt'][i]:>10.1f}   | "
                f"{st['rpm'][i]:>9d}    | {raw if raw == '-' else f'{raw:>9d}'}")
        lines.append("-----+----------------+----------------+---------------+----------------")
        lines.append(f"  sbus_frame_cnt={st['sbus_frame_cnt']}  "
                     f"sbus_ok_cnt={st['sbus_ok_cnt']}  "
                     f"帧数={self._frames}  周期={self.interval_ms}ms  "
                     f"(Ctrl+C 退出)")
        return lines

    def run(self):
        use_ansi = enable_vt()
        try:
            while not self._stop.is_set():
                st = self._latest
                lines = self._render_table(st)
                if use_ansi:
                    sys.stdout.write("\x1b[H\x1b[J")          # 清屏回到左上角
                    sys.stdout.write("\n".join(lines))
                else:
                    # 无 ANSI 支持：单行覆盖
                    sys.stdout.write("\r" + " | ".join(lines[1:-1])[:120].ljust(120))
                sys.stdout.flush()
                time.sleep(0.2)
        except KeyboardInterrupt:
            print("\n用户中断 (Ctrl+C)...")
        finally:
            self.close()

    def close(self):
        """停止读线程并发送 UNSUBSCRIBE 干净退出。"""
        self._stop.set()
        if self._thread.is_alive():
            self._thread.join(timeout=1.0)
        try:
            self.ser.write(build_frame(CMD_UNSUBSCRIBE))
            self._wait_ack(CMD_UNSUBSCRIBE, timeout=0.5)
            print("已发送 UNSUBSCRIBE，状态上报已关闭。")
        finally:
            if self.ser.is_open:
                self.ser.close()
                print("串口已关闭。")
            if self._csv_fp is not None:
                self._csv_fp.close()
                print("CSV 已保存。")


def main():
    setup_console()
    parser = argparse.ArgumentParser(description="Motor Driver Controller 状态监控")
    parser.add_argument("--port", default=None, help="串口号，如 COM5；缺省自动选择第一个 CH340")
    parser.add_argument("--interval", type=int, default=50,
                        help="上报周期 ms（默认 50；固件最低 20ms）")
    parser.add_argument("--csv", default=None, help="CSV 记录路径（可选）")
    args = parser.parse_args()

    port = args.port
    if port is None:
        from serial.tools import list_ports
        cands = [p for p in list_ports.comports()
                 if p.vid == 0x1A86 or "CH340" in (p.description or "").upper()]
        if not cands:
            print("未找到 CH340 串口，请用 --port 手动指定。")
            return
        port = cands[0].device
        print(f"自动选择串口: {port}")

    try:
        mon = StatusMonitor(port, args.interval, args.csv)
    except (serial.SerialException, RuntimeError, ValueError) as e:
        print(f"[错误] {e}")
        return

    print(f"已订阅 STATUS_REPORT: 周期 {args.interval}ms @ {port}")
    if args.csv:
        print(f"CSV 记录: {args.csv}")
    print("Ctrl+C 退出\n")
    mon.run()


if __name__ == "__main__":
    main()
