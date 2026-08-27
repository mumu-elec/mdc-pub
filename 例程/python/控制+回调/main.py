#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
控制+回调 —— 用 mdc_lite_ctrl 发控制帧并接收速度回调
====================================================

核心思想：上位机调参、下位机执行。程序用 mdc_lite_ctrl.MDLite 注册一个速度回调，
订阅 0xF0 状态上报后，边发 0x31 控制帧、边把由下位机主动推送的四通道转速
交回调打印。串口收发仍由本程序实现，库只负责「打包发送侧 + 解析回调侧」。

    from mdc_lite_ctrl import MDLite
    def on_speed(rpm): print("实时 rpm:", rpm)     # rpm = (rpm0, rpm1, rpm2, rpm3)
    mdc = MDLite(on_speed)
    ser.write(mdc.subscribe(50))
    while True:
        ser.write(mdc.ctrl(100, -200, 0, 300))
        for b in ser.read(64):
            mdc.feed(b)                            # 0xF0 到达时自动回调 on_speed

用法：
    python main.py --port COM5                 # 连真机
    python main.py --port COM5 --interval 50 --target "100 -200 0 300"
    python main.py --demo [--interval 50]      # 离线：喂一帧合成 0xF0，展示回调

⚠️ 经 USB 做实时控制前，下位机需已设 `/priority 1`（USB 主控），并建议 /save 持久化。
"""

import argparse
import struct
import sys
import time

import mdc_lite            # 发送侧（mdc_lite_ctrl 内部复用）
import mdc_lite_ctrl       # 调用+回调接收：MDLite(on_speed)


def on_speed(rpm):
    """速度回调：rpm 为四通道转速元组 (rpm0, rpm1, rpm2, rpm3)。"""
    print("\r实时转速 rpm = (%5d %5d %5d %5d)   " % tuple(rpm), end="", flush=True)


def run_live(port, interval_ms, targets):
    import serial
    mdc = mdc_lite_ctrl.MDLite(on_speed)
    ser = serial.Serial(port, 2000000, timeout=0.02)
    ser.reset_input_buffer()

    # 先订阅（收速度的前提）
    ser.write(mdc.subscribe(interval_ms))
    print("已订阅 0xF0（%dms）… 发送控制帧 Ctrl+C 退出。" % interval_ms)

    try:
        next_tick = time.monotonic()
        while True:
            ser.write(mdc.ctrl(*targets))            # 0x31 四通道目标值
            for b in ser.read(128):                  # 把收到的字节喂给回调接收器
                mdc.feed(b)
            next_tick += interval_ms / 1000.0
            d = next_tick - time.monotonic()
            if d > 0:
                time.sleep(d)
    except KeyboardInterrupt:
        print("\n用户中断，正在归零退出…")
    finally:
        ser.write(mdc.stop())                        # 急停：四通道全零
        ser.write(mdc.unsubscribe())                 # 取消订阅
        ser.close()
        print("串口已关闭。")


def run_demo(interval_ms):
    """离线演示：构造一帧 56B 的 0xF0 状态帧喂给接收器，观察回调。"""
    mdc = mdc_lite_ctrl.MDLite(on_speed)
    rpm = (1234, -567, 0, 9000)
    payload = (struct.pack("<4i", 100, 200, 300, 400) +        # enc
               struct.pack("<4f", 10.0, 20.0, 0.0, 5.0) +      # tgt
               struct.pack("<4i", *rpm) +                      # rpm
               struct.pack("<II", 7, 6))                       # sbus_frame_cnt, sbus_ok_cnt
    frame = mdc_lite._md.md_build_frame(0xF0, payload)         # 用库组帧
    print("=== 离线演示：连续喂入合成 0xF0 状态帧（56B） ===")
    for _ in range(3):
        for b in frame:
            mdc.feed(b)
        print()
    print("请用 --port 连接真机实际接收下位机上报。")


def main():
    ap = argparse.ArgumentParser(description="控制+回调：mdc_lite_ctrl 发控制帧+收速度回调")
    ap.add_argument("--port", default=None, help="串口号，如 COM5")
    ap.add_argument("--interval", type=int, default=50, help="订阅周期 ms（默认 50）")
    ap.add_argument("--target", default="100 -200 0 300",
                    help="四通道目标值，空格分隔（开环 PWM±1000 / 速度 RPM / 位置 0.1°）")
    ap.add_argument("--demo", action="store_true", help="离线演示（喂合成 0xF0 帧）")
    args = ap.parse_args()

    targets = [int(v) for v in args.target.split()[:4]]
    if len(targets) < 4:
        targets += [0] * (4 - len(targets))

    if args.demo or args.port is None:
        run_demo(args.interval)
    else:
        run_live(args.port, args.interval, targets)


if __name__ == "__main__":
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    main()
