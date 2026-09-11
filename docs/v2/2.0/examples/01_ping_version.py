# -*- coding: utf-8 -*-
"""
01_ping_version.py — 首次连接：PING + 读版本

对应《技术手册》§5.4（ACK）/ §5.5（PING）/ §5.2（/version）。
运行前：pip install pyserial，并把 SERIAL_PORT 改成你的 CH340 端口号。
"""

from mdc import Mdc, build_frame

SERIAL_PORT = "COM5"        # ← Windows 例：COM5；Linux 例：/dev/ttyUSB0


def main():
    mdc = Mdc(SERIAL_PORT)
    try:
        # 1) 二进制 PING（0x01）
        #    帧字节手工推演：crc8([0x01,0x00]) = 0x15 → 帧为 AA 01 00 15
        #    成功 ACK：     crc8([0x01,0x01,0x00]) = 0x7E → 回为 AA 01 01 00 7E
        print("PING 帧字节 :", build_frame(0x01).hex(" ").upper())
        print("PING 期望ACK : AA 01 01 00 7E")
        print("PING 结果    :", "OK" if mdc.ping() else "FAIL（检查电源/端口/波特率）")

        # 2) 文本指令 /version（应答形如 HW: v1.1.0  SW: v1.2.0）
        print("---- /version ----")
        print(mdc.cmd("/version"))

        # 3) /check 看实时状态（编码器/RPM/输出/运行时间）
        print("---- /check ----")
        print(mdc.cmd("/check"))
    finally:
        mdc.close()


if __name__ == "__main__":
    main()
