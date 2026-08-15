#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
demo_ping.py — 03_binary_protocol 例程：mdc_lib 二进制 API 调用示例
====================================================================
连接后依次演示 mdc_lib 的调用：

  1) md_text_version()        —— 文本指令查版本（mdc_lib 构造，打印回显）
  2) md_bin_ping()            —— 0x01 PING：发送后把收到的字节逐字节喂给
                                 MDParser 流式解析器，得到 ACK 帧后
                                 用 md_parse_ack 校验结果（err=0 成功）
  3) md_bin_read_param()      —— 0x10 READ_PARAM：接收 231B config_t 应答，
                                 用 md_parse_config 解析为 dict 并打印
                                 版本区/字段

帧的组帧、CRC8、帧解析、字段解析全部由 mdc_lib 完成，本脚本只负责串口收发。

用法：
    python demo_ping.py                # 自动选择第一个 CH340
    python demo_ping.py --port COM5    # 指定串口
"""

import argparse
import time

import serial
from serial.tools import list_ports

# 加载本仓库 mdc_lib（正式工程：复制 mdc_lib/python/mdc_lib.py 到项目目录即可）
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "mdc_lib", "python"))
import mdc_lib

BAUDRATE = 2000000
CH340_VID = 0x1A86


def setup_console():
    """控制台输出统一 UTF-8（兼容 GBK / 输出重定向场景）。"""
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass


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


class SerialLink:
    """串口收发封装：发送 mdc_lib 打包好的帧，用 MDParser 流式解析应答。

    MDParser 自动找 0xAA 同步字并做 CRC8 校验，与二进制帧混流的文本回显
    会被当作噪声丢弃，因此可以放心连续调用。
    """

    def __init__(self, port, baudrate=BAUDRATE):
        self.ser = serial.Serial(port, baudrate, timeout=0.05)
        self.ser.reset_input_buffer()
        self.parser = mdc_lib.MDParser()

    def send(self, frame):
        """发送 mdc_lib 打包好的一帧（bytes）。"""
        self.ser.write(frame)

    def wait_frame(self, cmd, timeout=1.0):
        """等待指定命令的一帧，返回 DATA 段 payload bytes；超时返回 None。"""
        deadline = time.monotonic() + timeout
        while True:
            remain = deadline - time.monotonic()
            if remain <= 0:
                return None
            chunk = self.ser.read(self.ser.in_waiting or 1)
            if not chunk:
                continue
            for b in chunk:
                r = self.parser.feed(b)          # 逐字节喂给流式解析器
                if r is not None and r[0] == cmd:
                    return r[1]

    def read_echo(self, timeout=0.8, quiet=0.1):
        """读取文本回显：quiet 秒无新数据视为回复结束。"""
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

    def close(self):
        if self.ser.is_open:
            self.ser.close()


def main():
    setup_console()
    parser = argparse.ArgumentParser(description="mdc_lib 二进制 API 调用示例（PING + READ_PARAM）")
    parser.add_argument("--port", default=None, help="串口号，如 COM5；缺省自动选择第一个 CH340")
    args = parser.parse_args()

    port = pick_port(args.port)
    if port is None:
        print("未找到 CH340 串口，请用 --port 手动指定。")
        return 1

    link = SerialLink(port)
    print(f"已打开 {port} @ {BAUDRATE}-8N1\n")

    try:
        # 1) 文本指令查版本（mdc_lib 构造 /version）
        print(">>> 文本指令 /version（mdc_lib.md_text_version() 构造）")
        link.send(mdc_lib.md_text_version())
        echo = link.read_echo().decode("utf-8", errors="replace").rstrip()
        print(echo if echo else "    (无回显)")

        # 2) PING：md_bin_ping() 打包 0x01 帧 → MDParser 读 ACK → md_parse_ack 校验
        print("\n>>> 二进制命令 0x01 PING（mdc_lib.md_bin_ping() 打包）")
        link.send(mdc_lib.md_bin_ping())
        payload = link.wait_frame(mdc_lib.MD_CMD_PING)
        if payload is None:
            print("[PING] 超时：未收到 ACK。请检查连接与波特率（2000000-8N1）。")
            return 1
        ack = mdc_lib.md_parse_ack(payload)      # ACK DATA 段 1 字节 err
        if ack.err == mdc_lib.MD_ERR_OK:
            print("[PING] 成功 (ACK err=0x00) -- 设备在线 [OK]")
        else:
            print(f"[PING] 失败 (ACK err=0x{ack.err:02X})")
            return 1

        # 3) READ_PARAM：md_bin_read_param() 打包 0x10 帧 → 接收 231B → md_parse_config 解析
        print("\n>>> 二进制命令 0x10 READ_PARAM（mdc_lib.md_bin_read_param() 打包）")
        link.send(mdc_lib.md_bin_read_param())
        raw = link.wait_frame(mdc_lib.MD_CMD_READ_PARAM, timeout=2.0)
        if raw is None or len(raw) != mdc_lib.MD_CONFIG_SIZE:
            print(f"[READ_PARAM] 超时/长度不符（期望 {mdc_lib.MD_CONFIG_SIZE}B）")
            return 1
        print(f"[READ_PARAM] 收到 config_t {len(raw)}B（期望 {mdc_lib.MD_CONFIG_SIZE}B）")
        print(f"  前 16 字节: {raw[:16].hex(' ')}")
        print("  (offset 0~10 为受保护区：magic/硬件版本/固件版本/crc，只读；")
        print("   版本号可直接看上方 /version 回显)")

        cfg = mdc_lib.md_parse_config(raw)       # 231B → dict（键名见 mdc_lib/API.md §6.5）
        print("\n  md_parse_config 解析结果（节选）：")
        print(f"    baud_rate     = {cfg['baud_rate']}")
        print(f"    cmd_timeout_ms= {cfg['cmd_timeout_ms']}")
        print(f"    protocol      = {cfg['protocol']}  (1=SBUS 2=UART 3=ELRS)")
        print(f"    sbus_inv      = {cfg['sbus_inv']}    ctrl_priority = {cfg['ctrl_priority']}")
        print(f"    control_mode  = {cfg['control_mode']}   (0=开环 1=速度 2=位置)")
        print(f"    encoder_cpr   = {cfg['encoder_cpr']}")
        print(f"    speed_kp      = {cfg['speed_kp']}")
        print(f"    speed_ki      = {cfg['speed_ki']}")
        print(f"    sbus_channel  = {cfg['sbus_channel']}   (遥控通道映射, 1~16)")
        print("\n演示完成 [OK]")
        return 0
    finally:
        link.close()
        print(f"串口 {port} 已关闭。")


if __name__ == "__main__":
    sys.exit(main())
