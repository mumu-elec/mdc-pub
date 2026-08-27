#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
05_status_monitor — 状态监控例程
=================================
订阅 0xF0 STATUS_REPORT 周期上报：
  1) mdc_lib.md_bin_subscribe(interval) 打包 0x40 SUBSCRIBE 帧并发送；
  2) 后台读线程把每个字节喂给 MDParser 流式解析器；
  3) 解析出 cmd==0xF0 的 payload 后用 mdc_lib.md_parse_status 解析
     （常规 56B / 扩展 72B 按长度自动兼容）；
  4) 控制台实时表格刷新（ANSI 清屏 / \\r 单行覆盖），可选 --csv 每帧记录一行。
Ctrl+C 干净退出：停止读线程 → 发送 mdc_lib.md_bin_unsubscribe() → 关闭串口。

用法：
    python status_monitor.py                 # 自动选择第一个 CH340, 50ms 周期
    python status_monitor.py --port COM5 --interval 20
    python status_monitor.py --csv log.csv   # 同时记录 CSV
"""

import argparse
import csv
import threading
import time

import serial

# mdc_lib 已随例程内置（本目录 mdc_lib.py）；如需更新库版本，用 ../mdc_lib/python/mdc_lib.py 覆盖
import os, sys
import mdc_lib

BAUDRATE = 2000000


def setup_console():
    """控制台输出统一 UTF-8（兼容 GBK / 输出重定向场景）。"""
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass


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
    """订阅状态上报：后台读线程喂字节给 MDParser + 主线程表格刷新。"""

    def __init__(self, port, interval_ms, csv_path=None):
        if interval_ms < 20:
            raise ValueError(f"固件最低上报周期 20ms，实际 {interval_ms}ms")
        self.interval_ms = interval_ms
        self.ser = serial.Serial(port, BAUDRATE, timeout=0.05)
        self.ser.reset_input_buffer()
        self.parser = mdc_lib.MDParser()   # 流式解析器：自动找 0xAA 同步 + CRC8 校验
        self._latest = None                # 最近一帧解析结果（md_status_t）
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

        # 开启订阅：md_bin_subscribe 打包 0x40 帧 → 同步等 ACK（此时解析器单线程使用）
        self.ser.write(mdc_lib.md_bin_subscribe(interval_ms))
        if self._wait_ack(mdc_lib.MD_CMD_SUBSCRIBE) != mdc_lib.MD_ERR_OK:
            raise RuntimeError("SUBSCRIBE ACK 失败或超时")

        self._t0 = time.monotonic()
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    def _wait_ack(self, cmd, timeout=0.5):
        """等待指定命令的 ACK（跳过其它帧）。返回 err 字节或 None。"""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            chunk = self.ser.read(self.ser.in_waiting or 1)
            for b in chunk:
                r = self.parser.feed(b)
                if r is not None and r[0] == cmd and len(r[1]) == 1:
                    return r[1][0]
        return None

    # ── 后台读线程 ────────────────────────────────────────
    def _reader(self):
        while not self._stop.is_set():
            chunk = self.ser.read(4096)     # 阻塞 ≤ 串口 timeout(0.05s)
            for b in chunk:
                r = self.parser.feed(b)     # 逐字节喂给流式解析器
                if r is None or r[0] != mdc_lib.MD_CMD_STATUS_REPORT:
                    continue
                try:
                    st = mdc_lib.md_parse_status(r[1])   # 56B/72B 自动兼容
                except ValueError:
                    continue
                self._latest = st
                self._frames += 1
                if self._csv_writer is not None:
                    raw = list(st.rpm_raw) if st.extended else [""] * 4
                    self._csv_writer.writerow(
                        [f"{time.monotonic() - self._t0:.3f}"] + list(st.enc) +
                        [f"{v:.1f}" for v in st.tgt] + list(st.rpm) +
                        list(raw) + [st.sbus_frame_cnt, st.sbus_ok_cnt])
                    self._csv_fp.flush()

    # ── 显示 ──────────────────────────────────────────────
    def _render_table(self, st):
        if st is None:
            return ["(等待第一帧 STATUS_REPORT...)"] * 6
        lines = ["  CH |    enc(脉冲)   |    tgt(当前)   |   rpm(滤波)   | rpm_raw(滤波前)"]
        lines.append("-----+----------------+----------------+---------------+----------------")
        for i in range(4):
            raw = st.rpm_raw[i] if st.extended else "-"
            lines.append(
                f"  {i + 1}  | {st.enc[i]:>12d}  | {st.tgt[i]:>10.1f}   | "
                f"{st.rpm[i]:>9d}    | {raw if raw == '-' else f'{raw:>9d}'}")
        lines.append("-----+----------------+----------------+---------------+----------------")
        lines.append(f"  sbus_frame_cnt={st.sbus_frame_cnt}  "
                     f"sbus_ok_cnt={st.sbus_ok_cnt}  "
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
            self.ser.write(mdc_lib.md_bin_unsubscribe())      # 0x41 关闭上报
            self._wait_ack(mdc_lib.MD_CMD_UNSUBSCRIBE, timeout=0.5)
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
