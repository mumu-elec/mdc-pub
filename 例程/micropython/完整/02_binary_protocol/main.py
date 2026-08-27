# -*- coding: utf-8 -*-
"""
02_binary_protocol — 二进制协议例程（ESP32 + MicroPython，基于 mdc_lib）

功能：演示 mdc_lib 二进制 API 的完整调用链路：
  1. md_bin_ping()       —— 打包 0x01 PING 帧发送；MDParser 逐字节收帧，
                            md_parse_ack() 解析 ACK（err=0x00 成功）
  2. md_bin_read_param() —— 打包 0x10 READ_PARAM 帧发送；收 231B config_t 应答，
                            md_parse_config() 解析全字段并打印关键项；
                            magic / 硬件版本 / 固件版本位于应答前 11 字节受保护区
                            （offset 0~10，mdc_lib 不解析该区，按《协议规范.md》§5 偏移直接读取）

串口收发留在本例程（用户侧代码）；协议打包/解析一律调用 mdc_lib：
  - md_bin_ping()            -> b"\\xAA\\x01\\x00\\x15"（PING 帧）
  - MDParser.feed(byte)      -> 完整帧返回 (cmd, payload)，否则 None
  - md_parse_ack(payload)    -> Ack(cmd, err)
  - md_bin_read_param()      -> 0x10 READ_PARAM 帧
  - md_parse_config(payload) -> dict（231B config_t 全字段）

运行前：控制板 USART2 需已配置为 UART 模式（/uart2 115200 0 uart）；
mdc_lib.py 与 main.py 同目录上传到设备。
"""

from machine import UART, Pin
import utime
import mdc_lib

# ---------------- 用户可调参数（按板子修改） ----------------
UART_ID = 2            # ESP32 UART 编号
TX_PIN = 17            # ESP32 TX -> 控制板 RC 信号（USART2 RX）
RX_PIN = 16            # ESP32 RX <- 控制板 RC 信号（USART2 TX）
BAUD = 115200          # 波特率，须与控制板 /uart2 配置一致
PING_TIMEOUT_MS = 500  # PING ACK 等待超时（ms）
READ_TIMEOUT_MS = 1500 # READ_PARAM 应答等待超时（231B @115200 约 21ms，留足余量）
RXBUF = 1024           # UART 接收缓冲区，须 >= 235B（最大整帧）
# ------------------------------------------------------------

uart = UART(UART_ID, baudrate=BAUD, tx=Pin(TX_PIN), rx=Pin(RX_PIN),
            rxbuf=RXBUF, timeout=READ_TIMEOUT_MS)


def wait_frame(parser, timeout_ms):
    """循环读 UART 字节喂给 MDParser，收到完整帧返回 (cmd, payload)；超时返回 None。"""
    start = utime.ticks_ms()
    while utime.ticks_diff(utime.ticks_ms(), start) < timeout_ms:
        n = uart.any()
        if n:
            for b in uart.read(n):
                r = parser.feed(b)
                if r:
                    return r
        else:
            utime.sleep_ms(5)
    return None


def read_u32_le(b, off):
    """读 4B 小端 uint32（仅用于受保护区 magic 字段，mdc_lib 不解析该区）。"""
    return b[off] | (b[off + 1] << 8) | (b[off + 2] << 16) | (b[off + 3] << 24)


def main():
    print("=== 02 Binary Protocol (mdc_lib) ===")
    parser = mdc_lib.MDParser()

    # 1) PING（0x01）：md_bin_ping() 打包 -> 发送 -> MDParser 收帧 -> md_parse_ack 校验
    print("-- PING (0x01) --")
    print("发送: %s" % " ".join("%02X" % b for b in mdc_lib.md_bin_ping()))
    uart.write(mdc_lib.md_bin_ping())
    r = wait_frame(parser, PING_TIMEOUT_MS)
    if r is None:
        print("PING 超时！请检查：接线 / 共地 / 控制板已执行 /uart2 115200 0 uart")
        return
    cmd, payload = r
    if cmd != mdc_lib.MD_CMD_PING:
        print("收到非 PING 应答帧: 0x%02X（忽略）" % cmd)
        return
    ack = mdc_lib.md_parse_ack(payload)          # 1B err -> Ack(cmd=None, err)
    if ack.err == mdc_lib.MD_ERR_OK:
        print("PING OK (err=0x00)")
    else:
        print("PING 失败 (err=0x%02X)" % ack.err)
        return

    # 2) READ_PARAM（0x10）：md_bin_read_param() 打包 -> 发送 -> 收 231B config_t
    print("-- READ_PARAM (0x10) --")
    uart.write(mdc_lib.md_bin_read_param())
    r = wait_frame(parser, READ_TIMEOUT_MS)
    if r is None:
        print("READ_PARAM 超时！")
        return
    cmd, payload = r
    if cmd != mdc_lib.MD_CMD_READ_PARAM or len(payload) != mdc_lib.MD_CONFIG_SIZE:
        print("应答异常: cmd=0x%02X len=%d B（期望 0x10 / %d B）"
              % (cmd, len(payload), mdc_lib.MD_CONFIG_SIZE))
        return

    print("config_t 长度: %d B (期望 %d)" % (len(payload), mdc_lib.MD_CONFIG_SIZE))

    # 2a) 受保护区（offset 0~10）：magic / 硬件版本 / 固件版本（协议规范 §5）
    print("受保护区 (offset 0~10):")
    print("  magic      = 0x%08X" % read_u32_le(payload, 0))
    print("  硬件版本   = v%d.%d.%d" % (payload[4], payload[5], payload[6]))
    print("  固件版本   = v%d.%d  (协议版本 D=%d)" % (payload[7], payload[8], payload[7]))

    # 2b) md_parse_config() 解析 231B 全字段 dict，打印关键项
    cfg = mdc_lib.md_parse_config(payload)
    print("关键配置字段 (md_parse_config):")
    print("  baud_rate    = %d" % cfg["baud_rate"])
    print("  cmd_timeout  = %d ms" % cfg["cmd_timeout_ms"])
    print("  control_mode = %s  (0=开环 1=速度 2=位置)" % cfg["control_mode"])
    print("  encoder_cpr  = %s" % cfg["encoder_cpr"])
    print("  speed_kp     = %s" % cfg["speed_kp"])

    print("=== 完成 ===")


main()
