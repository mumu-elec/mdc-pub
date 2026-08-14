# -*- coding: utf-8 -*-
"""
01_uart_hello — UART 最小连通例程（ESP32 + MicroPython）

功能：
  1. 初始化 ESP32 UART2（TX=GPIO17, RX=GPIO16, 115200）
  2. 发送文本指令 /version\n，循环读取控制板回显并打印（带超时）
  3. 再发送 /status\n，读取并打印控制板全部配置参数

前置条件：
  控制板 USART2 已配置为 UART 模式（先用 USB 上位机发送文本指令：
  /uart2 115200 0 uart），且 ESP32 与控制板共地。
"""

from machine import UART, Pin
import utime

# ---------------- 用户可调参数（按板子修改） ----------------
UART_ID = 2            # ESP32 UART 编号
TX_PIN = 17            # ESP32 TX -> 控制板 RC 信号（USART2 RX）
RX_PIN = 16            # ESP32 RX <- 控制板 RC 信号（USART2 TX）
BAUD = 115200          # 波特率，须与控制板 /uart2 配置一致
TIMEOUT_MS = 2000      # 读取回显的总超时（ms）
IDLE_MS = 300          # 回显结束判定：连续 300ms 无新数据视为回复完毕
RXBUF = 1024           # UART 接收缓冲区大小（字节）
# ------------------------------------------------------------

uart = UART(UART_ID, baudrate=BAUD, tx=Pin(TX_PIN), rx=Pin(RX_PIN),
            rxbuf=RXBUF, timeout=TIMEOUT_MS)


def read_all(timeout_ms=TIMEOUT_MS, idle_ms=IDLE_MS):
    """循环读取 UART 上所有可用数据，直到超时或空闲 idle_ms。返回 bytes。"""
    buf = b""
    start = utime.ticks_ms()
    last = start
    while utime.ticks_diff(utime.ticks_ms(), start) < timeout_ms:
        n = uart.any()
        if n:
            buf += uart.read(n)          # 有新数据：追加并刷新空闲计时
            last = utime.ticks_ms()
        elif buf and utime.ticks_diff(utime.ticks_ms(), last) > idle_ms:
            break                        # 已有数据且空闲 -> 认为回复结束
        utime.sleep_ms(5)
    return buf


def show(buf):
    """尽量按 UTF-8 文本打印；无法解码时以 repr 形式打印原始字节。"""
    try:
        print(buf.decode("utf-8"), end="")
    except Exception:
        print(repr(buf))


def send_cmd(cmd, timeout_ms=TIMEOUT_MS):
    """发送一条文本指令（自动补 \n）并读取回显打印。"""
    print(">>> %s" % cmd)
    uart.write(cmd + "\n")
    show(read_all(timeout_ms))


def main():
    print("=== Motor Driver Controller UART Hello (ESP32) ===")
    print("UART%d  TX=GPIO%d  RX=GPIO%d  @ %d baud" % (UART_ID, TX_PIN, RX_PIN, BAUD))

    # 1) 版本查询
    send_cmd("/version")

    # 2) 状态查询（打印全部配置参数）
    send_cmd("/status")

    print("=== 完成 ===")


main()
