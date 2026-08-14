#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
demo_ping.py — 03_binary_protocol 例程：PING + READ_PARAM 演示
================================================================
连接后依次执行：
  1) ping()      — 打印 ACK 结果，确认设备在线
  2) read_param()— 打印 config_t 前 16 字节 hex，并解析受保护区（offset 0~10）
                   的 magic / 硬件版本 / 固件版本字段

用法：
    python demo_ping.py                # 自动选择第一个 CH340
    python demo_ping.py --port COM5    # 指定串口
"""

import argparse
import struct
import sys

from motor_driver import ACK_OK, CONFIG_SIZE, MotorDriver

CONFIG_MAGIC = 0x4D445200   # 'MDR\0'（固件 config.h: CONFIG_MAGIC，LE 存储）


def setup_console():
    """控制台输出统一 UTF-8（兼容 GBK / 输出重定向场景）。"""
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass


def parse_header(raw):
    """解析 config_t 前 11 字节受保护区（offset 0~10，布局见固件 config.h）。"""
    magic = struct.unpack_from("<I", raw, 0)[0]
    hw_major, hw_minor, hw_variant = raw[4], raw[5], raw[6]
    sw_major, sw_patch = raw[7], raw[8]
    reserved, crc = raw[9], raw[10]
    return {"magic": magic,
            "hw": (hw_major, hw_minor, hw_variant),
            "sw": (sw_major, sw_patch),
            "reserved": reserved, "crc": crc}


def pick_port(explicit):
    if explicit:
        return explicit
    from serial.tools import list_ports
    cands = [p for p in list_ports.comports()
             if p.vid == 0x1A86 or "CH340" in (p.description or "").upper()]
    if not cands:
        return None
    print(f"自动选择: {cands[0].device} ({cands[0].description})")
    return cands[0].device


def main():
    setup_console()
    parser = argparse.ArgumentParser(description="PING + READ_PARAM 演示")
    parser.add_argument("--port", default=None, help="串口号，如 COM5；缺省自动选择第一个 CH340")
    args = parser.parse_args()

    port = pick_port(args.port)
    if port is None:
        print("未找到 CH340 串口，请用 --port 手动指定。")
        return 1

    with MotorDriver(port) as drv:
        # 1) PING
        err = drv.ping()
        if err is None:
            print("[PING] 超时：未收到 ACK。请检查连接与波特率（2000000-8N1）。")
            return 1
        if err == ACK_OK:
            print("[PING] 成功 (ACK err=0x00) -- 设备在线 [OK]")
        else:
            print(f"[PING] 失败 (ACK err=0x{err:02X})")
            return 1

        # 2) READ_PARAM
        cfg = drv.read_param()
        print(f"[READ_PARAM] 收到 config_t {len(cfg)}B（期望 {CONFIG_SIZE}B）")
        print(f"  前 16 字节: {cfg[:16].hex(' ')}")

        h = parse_header(cfg)
        print("  受保护区 (offset 0~10) 解析：")
        magic_ok = h["magic"] == CONFIG_MAGIC
        print(f"    magic       = 0x{h['magic']:08X}"
              f"  {'（与 CONFIG_MAGIC 一致 [OK]）' if magic_ok else '（不一致！可能固件/协议版本不匹配）'}")
        print(f"    硬件版本    = v{h['hw'][0]}.{h['hw'][1]}.{h['hw'][2]}")
        print(f"    固件版本    = v{h['sw'][0]}.{h['sw'][1]}  (SW_MAJOR/协议版本 D = {h['sw'][0]})")
        print(f"    reserved    = {h['reserved']}    crc = 0x{h['crc']:02X}")

        if not magic_ok:
            print("  ! 注意：上位机协议版本 D 必须与固件 SW_MAJOR 一致，否则协议不兼容（规范 §1）。")
            return 1
        return 0


if __name__ == "__main__":
    sys.exit(main())
