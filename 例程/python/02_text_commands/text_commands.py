#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
02_text_commands — 文本指令交互工具
====================================
封装文本指令的发送/回显读取（send / query），提供：
  * 交互式 REPL：直接输入任意 /xxx 指令，回车即发送并打印回显
  * 数字快捷菜单：1~8 一键执行常用指令，9 退出

用法：
    python text_commands.py                # 自动选择第一个 CH340
    python text_commands.py --port COM5    # 指定串口

⚠️ 实时控制注意事项（协议规范 §1）：
  * 上位机（USB）做实时控制前请先执行 `/priority 1`，否则控制帧受仲裁可能被拒绝；
  * `/timeout <ms>` 为指令超时保护：超过设定时间未收到控制指令，电机输出自动归零
    （0 = 关闭保护；同时作为优先级心跳窗口，最小 100ms）。
"""

import argparse
import sys
import time

import serial
from serial.tools import list_ports


def setup_console():
    """控制台输出统一 UTF-8（兼容 GBK / 输出重定向场景）。"""
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass

BAUDRATE = 2000000
CH340_VID = 0x1A86

# 数字快捷菜单：数字 → (指令, 说明)；9 为退出
MENU = [
    ("1", "/version",                       "查版本"),
    ("2", "/check",                         "查实时状态"),
    ("3", "/status",                        "查全部配置"),
    ("4", "/mode 1 speed",                  "设通道1为速度模式"),
    ("5", "/mode 1 open",                   "设通道1为开环"),
    ("6", "/speedctrl 1 0.5 0.02 0.01",     "速度环参数示例 (kp ki kd)"),
    ("7", "/enczero 1",                     "通道1编码器清零"),
    ("8", "/save",                          "保存到 EEPROM"),
    ("9", None,                             "退出"),
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


class TextTerminal:
    """文本指令终端：封装发送 + 回显读取。"""

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

    def send(self, cmd):
        """发送一行文本指令并打印回显（交互场景）。返回回显文本。"""
        line = cmd.strip()
        if not line:
            return ""
        print(f"\n>>> {line}")
        self.ser.write(line.encode("ascii", errors="replace") + b"\n")
        text = self.read_echo().decode("utf-8", errors="replace").rstrip()
        if text:
            print(text)
        else:
            print("    (无回显)")
        return text

    def query(self, cmd, timeout=1.0):
        """发送一行文本指令，返回完整回显文本（不打印）。"""
        self.ser.write(cmd.strip().encode("ascii", errors="replace") + b"\n")
        return self.read_echo(timeout=timeout).decode("utf-8", errors="replace")

    def close(self):
        if self.ser.is_open:
            self.ser.close()


def print_menu():
    print("\n-------- 快捷菜单 --------")
    for key, cmd, desc in MENU:
        if cmd:
            print(f"  [{key}] {desc:<14} -> {cmd}")
        else:
            print(f"  [{key}] {desc}")
    print("  [h] 显示本菜单      [q] 退出")
    print("  或直接输入任意 /xxx 指令发送")


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

            if line in ("q", "quit", "exit"):
                break
            if line in ("h", "help", "?"):
                print_menu()
                continue
            if line in ("9",):
                break

            # 数字快捷菜单
            for key, cmd, _ in MENU:
                if line == key and cmd:
                    term.send(cmd)
                    break
            else:
                # 直接输入指令
                if line.startswith("/"):
                    term.send(line)
                elif line:
                    print("  请输入 /xxx 指令，或输入 h 查看菜单。")
    except KeyboardInterrupt:
        print("\n用户中断 (Ctrl+C)，干净退出。")
    finally:
        term.close()
        print(f"串口 {port} 已关闭。")


if __name__ == "__main__":
    main()
