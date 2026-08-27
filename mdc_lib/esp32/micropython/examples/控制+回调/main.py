# -*- coding: utf-8 -*-
"""
控制+回调 — 发送控制帧 + 收速度回调（ESP32 + MicroPython，基于 mdc_lite_ctrl）
=============================================================================

**核心思想：上位机调参、下位机执行。** 本例在发送 0x31 控制帧的同时，
用 `mdc_lite_ctrl.MDLite` 流式接收下位机推送的 0xF0 STATUS_REPORT，
并在回调里打印四通道实时转速 rpm。

要点：
  - MDLite 封装了发送侧（ctrl/stop/subscribe/unsubscribe）与回调接收侧（feed）
  - 收速度的前提：先 subscribe(interval_ms) 开启 0x40 状态上报（固件钳位 >=20ms）
  - feed(b) 仅在收到完整且 CRC 通过的 0xF0 时回调 on_speed(rpm)；其余帧/噪声忽略
  - 走 RC 口（USART2）：默认 /priority 0 = RC 口优先，控制帧天然生效

运行前：控制板 USART2 需已配置为 UART 模式（/uart2 115200 0 uart）。
mdc_lib.py / mdc_lite.py / mdc_lite_ctrl.py 与 main.py 同目录上传。
"""

from machine import UART, Pin
import utime
from mdc_lite_ctrl import MDLite

# ---------------- 用户可调参数（按板子修改） ----------------
UART_ID = 2            # ESP32 UART 编号
TX_PIN = 17            # ESP32 TX -> 控制板 RC 信号（USART2 RX）
RX_PIN = 16            # ESP32 RX <- 控制板 RC 信号（USART2 TX）
BAUD = 115200          # 波特率，须与控制板 /uart2 配置一致
RXBUF = 1024           # UART 接收缓冲区大小（字节）
SUBSCRIBE_MS = 50      # 状态上报周期（固件钳位 >=20ms）
CTRL_PERIOD_MS = 50    # 0x31 控制帧发送周期
TARGET = 300           # 目标值（开环 PWM；速度=RPM；位置=0.1°）
CH = 0                 # 通道索引 0~3，对应 Motor A~D
# ------------------------------------------------------------

uart = UART(UART_ID, baudrate=BAUD, tx=Pin(TX_PIN), rx=Pin(RX_PIN),
            rxbuf=RXBUF)


def on_speed(rpm):
    """速度回调：下位机推送 0xF0 时被调用，rpm 为 [m0, m1, m2, m3]。"""
    print("实时转速 rpm: %+6d %+6d %+6d %+6d"
          % (rpm[0], rpm[1], rpm[2], rpm[3]))


mdc = MDLite(on_speed)     # 注册速度回调


def main():
    try:
        # ① 先订阅，开启 0x40 状态上报（收速度的前提）
        uart.write(mdc.subscribe(SUBSCRIBE_MS))
        print("已开启状态上报：每 %d ms 一次（0x40 SUBSCRIBE）" % SUBSCRIBE_MS)

        # ② 主循环：发控制帧 + 喂字节 -> 回调打印 rpm
        t = [0, 0, 0, 0]
        t[CH] = TARGET
        tgt = mdc.ctrl(t[0], t[1], t[2], t[3])
        print("持续发送 0x31 控制帧：ch%d -> %+d（Ctrl+C 退出）" % (CH + 1, TARGET))

        while True:
            uart.write(tgt)
            n = uart.any()
            if n:
                for b in uart.read(n):
                    mdc.feed(b)            # 0xF0 到达时自动回调 on_speed
            utime.sleep_ms(CTRL_PERIOD_MS)
    except KeyboardInterrupt:
        print("\n用户中断")
    finally:
        uart.write(mdc.stop())             # 急停
        uart.write(mdc.unsubscribe())      # 关闭上报（善后）
        print("已急停 (0x31 all=0) 并取消上报 (0x41)")


main()
