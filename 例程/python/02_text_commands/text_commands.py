#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
02_text_commands — 文本指令交互工具
====================================
交互式文本指令终端：
  * 直接输入任意文本指令（如 /speedctrl 1 0.5 0.02 0.01）发送并打印回显；
    指令统一交给 mdc_lib.md_text_build(cmd, args) 打包（缺 "/" 自动补上；
    带参数 = 写入 / 不带参数 = 读取模式，见协议规范 §2.1）。
  * 数字快捷菜单：快捷键同样由 mdc_lib 文本函数构造：
        1 = /version          2 = /check（实时状态）
        3 = /status（配置）   4 = /mode 1 speed
        5 = /mode 1 open      6 = /save
        7 = 退出
  * h 显示菜单，q 退出。

用法：
    python text_commands.py                # 自动选择第一个 CH340
    python text_commands.py --port COM5    # 指定串口

⚠️ 实时控制注意事项（协议规范 §1）：
  * USB 主控（PC）做实时控制前请先执行 `/priority 1`，否则控制帧（0x30/0x31）
    受仲裁可能被拒绝；
  * `/timeout <ms>` 为指令超时保护：超过设定时间未收到控制指令，电机输出自动归零
    （0 = 关闭保护；同时作为优先级心跳窗口，最小 100ms）。
"""

import argparse
import time

import serial
from serial.tools import list_ports

# mdc_lib 已随例程内置（本目录 mdc_lib.py）；如需更新库版本，用 ../mdc_lib/python/mdc_lib.py 覆盖
import os, sys
import mdc_lib


def setup_console():
    """控制台输出统一 UTF-8（兼容 GBK / 输出重定向场景）。"""
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass

BAUDRATE = 2000000
CH340_VID = 0x1A86

# 数字快捷菜单：键 → (说明, mdc_lib 打包好的指令 bytes)；"7" 为退出
# 快捷键指令全部由 mdc_lib 文本函数构造，发送时直接 ser.write(bytes)
MENU = [
    ("1", "查版本",        mdc_lib.md_text_version()),
    ("2", "查实时状态",    mdc_lib.md_text_check()),
    ("3", "查全部配置",    mdc_lib.md_text_status()),
    ("4", "通道1 速度模式", mdc_lib.md_text_mode(1, "speed")),
    ("5", "通道1 开环",     mdc_lib.md_text_mode(1, "open")),
    ("6", "保存到 EEPROM",  mdc_lib.md_text_save()),
    ("7", "退出",          None),
]


def is_ch340(port_info):
    if port_info.vid is not None and port_info.vid == CH340_VID:
        return True
    return "CH340" in (port_info.description or "").upper()


def pick_port(explicit):
    if explicit:
        return explicit
    ch340 = [p for p in list_ports.comports() if is_ch340(p)]
    if ch340:
        print(f"自动选择: {ch340[0].device} ({ch340[0].description})")
        return ch340[0].device
    return None


def build_cmd_bytes(line):
    """用户输入 → mdc_lib 打包：拆成 cmd + args，缺 "/" 自动补上。"""
    parts = line.strip().split(maxsplit=1)
    cmd = parts[0]
    args = parts[1] if len(parts) > 1 else None
    return mdc_lib.md_text_build(cmd, args)


class TextTerminal:
    """文本指令终端：串口收发由本类实现，指令打包调用 mdc_lib。"""

    def __init__(self, port, baudrate=BAUDRATE):
        self.ser = serial.Serial(port, baudrate, timeout=0.05)
        self.ser.reset_input_buffer()

    def read_echo(self, timeout=1.0, quiet=0.15):
        """读取回显：quiet 秒无新数据视为回复结束；总时长不超过 timeout 秒。"""
        data = b""
        last = time.monotonic()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            chunk = self.ser.read(self.ser.in_waiting or 1)
            if chunk:
                data += chunk
                last = time.monotonic()
            elif time.monotonic() - last >= quiet:
                break
        return data

    def send(self, cmd, label=None):
        """发送一条 mdc_lib 打包的文本指令（bytes，含 \\n）并打印回显。返回回显文本。"""
        if label is None:
            label = cmd.decode("utf-8").strip()
        print(f"\n>>> {label}")
        self.ser.write(cmd)
        text = self.read_echo().decode("utf-8", errors="replace").rstrip()
        if text:
            print(text)
        else:
            print("    (无回显)")
        return text

    def close(self):
        if self.ser.is_open:
            self.ser.close()


def print_menu():
    print("\n-------- 快捷菜单 --------")
    for key, desc, frame in MENU:
        if frame is not None:
            print(f"  [{key}] {desc:<12} -> {frame.decode('utf-8').strip()}")
        else:
            print(f"  [{key}] {desc}")
    print("  [h] 显示本菜单      [q] 退出")
    print("  或直接输入任意指令（如 /speedctrl 1 0.5 0.02 0.01，缺 '/' 自动补上）")


def main():
    setup_console()
    parser = argparse.ArgumentParser(description="Motor Driver Controller 文本指令交互工具")
    parser.add_argument("--port", default=None, help="串口号，如 COM5；缺省自动选择第一个 CH340")
    args = parser.parse_args()

    port = pick_port(args.port)
    if port is None:
        print("未找到 CH340 串口，请用 --port 手动指定。")
        return

    try:
        term = TextTerminal(port)
    except serial.SerialException as e:
        print(f"[错误] 串口打开失败: {e}")
        print("  请确认驱动已装、端口未被占用。")
        return

    print(f"已连接 {port} @ {BAUDRATE}-8N1（文本指令模式）")
    print("提示：实时控制前请先 /priority 1；失控保护可设 /timeout <ms>（0=关闭）。")
    print_menu()

    try:
        while True:
            try:
                line = input("\n>>> ").strip()
            except EOFError:
                print("\n输入结束，退出。")
                break

            if line in ("7", "q", "quit", "exit"):
                break
            if line in ("h", "help", "?"):
                print_menu()
                continue

            # 数字快捷菜单（指令已由 mdc_lib 打包好）
            for key, desc, frame in MENU:
                if line == key and frame is not None:
                    term.send(frame, label=desc)
                    break
            else:
                # 直接输入指令：md_text_build 构造（缺 "/" 自动补上）
                try:
                    term.send(build_cmd_bytes(line))
                except ValueError as e:
                    print(f"  [错误] {e}")
    except KeyboardInterrupt:
        print("\n用户中断 (Ctrl+C)，干净退出。")
    finally:
        term.close()
        print(f"串口 {port} 已关闭。")


if __name__ == "__main__":
    main()
