# -*- coding: utf-8 -*-
"""
02_params.py — 参数读写：全量读 / WRITE_FIELD 改单字段 / 保存与加载

对应《技术手册》§5.5（0x10/0x12/0x20/0x21）/ §5.7（config_t 布局）。

要点：
- 所有写入只改 RAM、立即生效；/save（0x20）才持久化到 EEPROM（约 190ms）；
- WRITE_FIELD 的 field_id 是 config_t 的字节偏移（小端 u16），
  offset 0~10 为受保护区（写入返回 0x02）；
- 推荐优先 READ_PARAM 全量读 → 改 → WRITE_PARAM 全量写，
  WRITE_FIELD 适合少量字段的轻量场景。
"""

import struct

from mdc import Mdc

SERIAL_PORT = "COM5"        # ← 改成你的 CH340 端口

# config_t 字段偏移速查（完整表见手册 §5.7）
OFF_CMD_TIMEOUT = 15        # cmd_timeout_ms, u16 LE, 超时保护 (ms, 0=关闭)


def main():
    mdc = Mdc(SERIAL_PORT)
    try:
        # 1) 全量读配置（248 字节）
        cfg = mdc.read_param()
        print("config_t 长度 :", len(cfg), "字节")
        print("魔数          :", hex(struct.unpack("<I", cfg[0:4])[0]), "(期望 0x4d445200)")
        print("协议版本 D/E  :", cfg[7], "/", cfg[8], "(D=2 需与上位机一致)")
        print("USART2 波特率 :", struct.unpack("<I", cfg[11:15])[0])

        # 2) WRITE_FIELD 改单字段：开启 1s 命令超时保护（超时后电机输出自动归零）
        print("\n当前 /timeout ->", mdc.cmd("/timeout"))
        mdc.write_field(OFF_CMD_TIMEOUT, struct.pack("<H", 1000))
        print("写入后 /timeout ->", mdc.cmd("/timeout"))

        # 3) 受保护字段演示：写 offset 0（magic）会被拒绝，返回 0x02
        try:
            mdc.write_field(0, b"\x00\x00\x00\x00")
        except IOError as e:
            print("受保护区写入（预期被拒）:", e)

        # 4) 持久化：只有 /save 后断电才不丢
        mdc.save()
        print("\n已保存到 EEPROM；/load 可放弃 RAM 修改重新加载。")
    finally:
        mdc.close()


if __name__ == "__main__":
    main()
