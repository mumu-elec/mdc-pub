#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
01_hello_serial — 最小连通性测试例程
====================================
连接 Motor Driver Controller 的 USB 虚拟串口（CH340N，固定 2000000-8N1），
用 mdc_lib 构造文本指令发送并打印回显：
    md_text_version()            ->  b"/version\\n"   查硬件/软件版本
    md_text_build("/status")     ->  b"/status\\n"    查全部配置参数

本例程演示「串口收发留在用户侧、协议打包调用 mdc_lib」的用法：
mdc_lib 只负责把指令打包成要发送的 bytes，串口的打开/写入/读取由本脚本完成。

用法：
    python hello_serial.py                # 自动选择第一个 CH340 串口
    python hello_serial.py --port COM5    # 指定串口
    python hello_serial.py --list         # 仅列出可用串口

依赖：pyserial（pip install pyserial）
"""

import argparse
import time

import serial
from serial.tools import list_ports

# 加载本仓库 mdc_lib（正式工程：复制 mdc_lib/python/mdc_lib.py 到项目目录即可）
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "mdc_lib", "python"))
import mdc_lib


def setup_console():
    """控制台输出统一 UTF-8（兼容 GBK / 输出重定向场景，防止打印非 ASCII 符号崩溃）。"""
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass

BAUDRATE = 2000000          # 固定波特率，不可更改（协议规范 §1）
CH340_VID = 0x1A86          # WCH CH340/CH341 的 USB VID


def is_ch340(port_info):
    """判断串口是否为 CH340：优先按 VID==0x1A86，其次按描述包含 CH340/CH341。"""
    if port_info.vid is not None and port_info.vid == CH340_VID:
        return True
    desc = (port_info.description or "").upper()
    return "CH340" in desc or "CH341" in desc


def pick_port(explicit):
    """解析串口：--port 优先；否则自动选第一个 CH340；都没有则返回 None。"""
    if explicit:
        return explicit
    ch340 = [p for p in list_ports.comports() if is_ch340(p)]
    if ch340:
        print(f"自动选择: {ch340[0].device} ({ch340[0].description})")
        return ch340[0].device
    return None


def read_echo(ser, timeout=1.0, quiet=0.15):
    """读取回显：连续 quiet 秒无新数据即认为回复结束；总时长不超过 timeout 秒。"""
    data = b""
    last = time.monotonic()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        chunk = ser.read(ser.in_waiting or 1)   # 串口 timeout 很短，无数据时立即返回
        if chunk:
            data += chunk
            last = time.monotonic()
        elif time.monotonic() - last >= quiet:
            break
    return data


def send_and_print(ser, cmd, label):
    """发送一条 mdc_lib 打包的文本指令（bytes，含 \\n）并打印回显。"""
    print(f"\n>>> 发送: {label}  ({cmd.decode('utf-8').strip()})")
    ser.write(cmd)
    echo = read_echo(ser)
    if echo:
        print(echo.decode("utf-8", errors="replace").rstrip())
    else:
        print("    (无回显 —— 请检查连接/波特率/设备是否已上电)")


def main():
    setup_console()
    parser = argparse.ArgumentParser(description="Motor Driver Controller 最小连通性测试")
    parser.add_argument("--port", default=None, help="串口号，如 COM5；缺省自动选择第一个 CH340")
    parser.add_argument("--list", action="store_true", help="仅列出可用串口后退出")
    args = parser.parse_args()

    # 1) 列出可用串口
    ports = list(list_ports.comports())
    print("可用串口:")
    if not ports:
        print("  (未发现任何串口 —— 请检查 USB 连接与 CH340 驱动)")
        return
    for p in ports:
        mark = "  [CH340]" if is_ch340(p) else ""
        print(f"  {p.device:<10}{p.description}{mark}")
    if args.list:
        return

    # 2) 确定端口
    port = pick_port(args.port)
    if port is None:
        print("\n未找到 CH340 串口，请用 --port 手动指定（例如 --port COM5）。")
        return

    # 3) 打开串口并测试
    ser = None
    try:
        ser = serial.Serial(port, BAUDRATE, timeout=0.05)
        ser.reset_input_buffer()          # 清空历史残留字节
        print(f"已打开 {port} @ {BAUDRATE}-8N1\n")

        # 文本指令由 mdc_lib 打包，串口收发在本脚本完成
        send_and_print(ser, mdc_lib.md_text_version(), "/version")
        send_and_print(ser, mdc_lib.md_text_build("/status"), "/status")
        print("\n连通性测试完成 [OK]")
    except serial.SerialException as e:
        print(f"\n[错误] 串口打开失败: {e}")
        print("  请确认：1) CH340 驱动已安装；2) 端口未被其它程序占用；")
        print("           3) 波特率固定 2000000-8N1（不可更改）。")
    except KeyboardInterrupt:
        print("\n用户中断 (Ctrl+C)，干净退出。")
    finally:
        if ser is not None and ser.is_open:
            ser.close()
            print(f"串口 {port} 已关闭。")


if __name__ == "__main__":
    main()
