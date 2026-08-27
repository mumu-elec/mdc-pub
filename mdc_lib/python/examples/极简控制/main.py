#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
极简控制 —— 只用 mdc_lite 发送控制帧（只看发送，不解析回包）
=============================================================

核心思想：上位机调参、下位机执行。本程序只演示「要发送的帧怎么来」——
全程只调用 mdc_lite（send-only），把要写往串口的帧打印出来。串口收发由你实现：

    import serial
    ser = serial.Serial("COM5", 2000000)
    ser.write(帧)          # 把 print 出来的那一帧补上 ser.write 即可

只涉及 3 条命令：0x31 MOTOR_CTRL、0x40/0x41 SUBSCRIBE/UNSUBSCRIBE。
若需回读下位机 0xF0 状态并回调 rpm，请改用「控制+回调」例程（mdc_lite_ctrl.MDLite）。

运行：python main.py        （无需硬件/串口，在终端打印各帧字节；也可 --port COM5 真正发送）
"""

import argparse
import sys

import mdc_lite            # 极简库（send-only）：ctrl/stop/subscribe/unsubscribe


def dump(tag, frame):
    """打印一帧字节（HEX），用于展示库打包结果。"""
    print("%-16s [%2d B] : %s" % (tag, len(frame), " ".join("%02X" % b for b in frame)))


def main():
    ap = argparse.ArgumentParser(description="极简控制：mdc_lite 只发送控制帧")
    ap.add_argument("--port", default=None, help="串口号，如 COM5。提供则实际发送；缺省仅打印帧")
    ap.add_argument("--interval-ms", type=int, default=50,
                    help="订阅上报周期 ms（默认 50，建议 ≥20）")
    args = ap.parse_args()

    print("=== mdc_lite 极简控制（仅发送控制帧） ===")
    # 1) 订阅状态上报（若需回读速度的前提；仅发送时可不订阅）
    dump("subscribe(%d)" % args.interval_ms, mdc_lite.subscribe(args.interval_ms))
    # 2) 四通道控制目标（int32 LE；含义随通道模式：open=PWM±1000 speed=RPM pos=0.1°）
    dump("ctrl(100,-200,0,300)", mdc_lite.ctrl(100, -200, 0, 300))
    # 3) 退出/急停前发送全零帧（0x31 DATA=16B 全零）
    dump("stop()", mdc_lite.stop())
    # 4) 取消订阅（善后）
    dump("unsubscribe()", mdc_lite.unsubscribe())

    if args.port:
        import serial
        ser = serial.Serial(args.port, 2000000, timeout=0.05)
        for tag, frame in (("subscribe", mdc_lite.subscribe(args.interval_ms)),
                           ("ctrl", mdc_lite.ctrl(100, -200, 0, 300)),
                           ("stop", mdc_lite.stop()),
                           ("unsubscribe", mdc_lite.unsubscribe())):
            ser.write(frame)
            print("已发送 %-12s -> %s" % (tag, " ".join("%02X" % b for b in frame)))
        ser.close()
        print("串口已关闭。")
    else:
        print("\n把上面每帧字节直接 ser.write() 即可发送给下位机执行。")
        print("提示：subscribe(50) 整帧 == AA 40 02 32 00 9E（CRC=0x9E）。")


if __name__ == "__main__":
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    main()
