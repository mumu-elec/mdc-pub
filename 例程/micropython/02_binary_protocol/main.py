# -*- coding: utf-8 -*-
"""
02_binary_protocol — 二进制帧协议 Demo（ESP32 + MicroPython）

流程：
  1. PING（0x01）       —— 连通性测试，打印 ACK 校验结果
  2. READ_PARAM（0x10） —— 读取 config_t（231B），打印前 16 字节 hex
  3. 结束

运行前：控制板 USART2 需已配置为 UART 模式（/uart2 115200 0 uart）。
"""

import ustruct
from motor_driver import MotorDriver, CONFIG_T_SIZE


def main():
    drv = MotorDriver()
    print("=== 02 Binary Protocol Demo ===")

    # 1) PING（0x01）：返回 True=成功 / False=失败 / None=超时
    print("-- PING (0x01) --")
    r = drv.ping(timeout_ms=500)
    if r is None:
        print("PING 超时！请检查：接线 / 共地 / 控制板已执行 /uart2 115200 0 uart")
    elif r:
        print("PING OK (err=0x00)")
    else:
        print("PING 失败 (err=0xFF)")

    # 2) READ_PARAM（0x10）：读取 config_t 全量配置（231B）
    print("-- READ_PARAM (0x10) --")
    cfg = drv.read_param(timeout_ms=1500)
    print("config_t 长度: %d B (期望 %d)" % (len(cfg), CONFIG_T_SIZE))
    print("前 16 字节 hex: %s" % cfg[:16].hex())
    print("offset 0~10 受保护区(不可写): %s" % cfg[:11].hex())
    # 示例字段解析（小端序，偏移见协议规范 §5）
    print("baud_rate      (offset 11, u32 LE): %d" % ustruct.unpack("<I", cfg[11:15])[0])
    print("cmd_timeout_ms (offset 15, u16 LE): %d" % ustruct.unpack("<H", cfg[15:17])[0])

    print("=== 完成 ===")


main()
