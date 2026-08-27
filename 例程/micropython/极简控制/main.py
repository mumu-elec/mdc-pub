# -*- coding: utf-8 -*-
"""
极简控制 — 只管发送控制帧（ESP32 + MicroPython，基于 mdc_lite send-only）
=============================================================================

**核心思想：上位机调参、下位机执行。** 本例只演示"发什么控制量"：
用 `mdc_lite.ctrl()` / `mdc_lite.stop()` 打包 0x31 控制帧并经 UART 发送
（可选 0x40 订阅 / 0x41 取消订阅），**不解析任何回包**。
若需读回转速，请用同级的"控制+回调"例程（mdc_lite_ctrl）。

要点：
  - mdc_lite 只负责打包要发送的帧（bytes），串口收发由本程序 uart.write() 完成
  - 0x31 MOTOR_CTRL 目标值含义随通道模式：开环=PWM(±1000)、速度=RPM、位置=0.1°(±3600)
  - 走 RC 口（USART2）：默认 /priority 0 = RC 口优先，控制帧天然生效
  - 演示序列：开环正转 -> 反转 -> 停止（每档 PHASE_MS，斜坡平滑过渡）

运行前：控制板 USART2 需已配置为 UART 模式（/uart2 115200 0 uart）。
mdc_lib.py 与 mdc_lite.py 与 main.py 同目录上传到设备。
"""

from machine import UART, Pin
import utime
import mdc_lite

# ---------------- 用户可调参数（按板子修改） ----------------
UART_ID = 2            # ESP32 UART 编号
TX_PIN = 17            # ESP32 TX -> 控制板 RC 信号（USART2 RX）
RX_PIN = 16            # ESP32 RX <- 控制板 RC 信号（USART2 TX）
BAUD = 115200          # 波特率，须与控制板 /uart2 配置一致
RXBUF = 1024           # UART 接收缓冲区大小（字节）
CH = 0                 # 通道索引 0~3，对应 Motor A~D（本例只控制通道 1）
STEP = 50              # 斜坡步长：每个周期向目标靠近 50（PWM 单位）
PERIOD_MS = 50         # 0x31 发送周期（协议建议 30/50/100ms）
TARGET_FWD = 300       # 开环正转目标 PWM（+300/1000）
TARGET_REV = -300      # 反转目标 PWM
TARGET_STOP = 0        # 停止
PHASE_MS = 2000        # 演示序列每档持续时间
# ------------------------------------------------------------

uart = UART(UART_ID, baudrate=BAUD, tx=Pin(TX_PIN), rx=Pin(RX_PIN),
            rxbuf=RXBUF)


def send_ctrl(current):
    """mdc_lite.ctrl() 打包 0x31 帧并发送（只动 CH 通道，其余填 0）。"""
    t = [0, 0, 0, 0]
    t[CH] = current
    uart.write(mdc_lite.ctrl(t[0], t[1], t[2], t[3]))


def ramp_to(target, current):
    """从 current 斜坡逼近 target，每 PERIOD_MS 发送一次 0x31，持续 PHASE_MS。

    到达 target 后继续发送保持（防止 /timeout 超时归零），持续 PHASE_MS 后返回。
    """
    start = utime.ticks_ms()
    while True:
        if current < target:
            current = min(current + STEP, target)
        elif current > target:
            current = max(current - STEP, target)
        send_ctrl(current)
        print("ch%d -> %+d" % (CH + 1, current))
        utime.sleep_ms(PERIOD_MS)
        if utime.ticks_diff(utime.ticks_ms(), start) >= PHASE_MS:
            break
    return current


def main():
    try:
        print("== 极简控制：只管发送 0x31 控制帧 ==")
        cur = 0
        print("[1/3] 开环正转 +%d" % TARGET_FWD)
        cur = ramp_to(TARGET_FWD, cur)
        print("[2/3] 反转 %d" % TARGET_REV)
        cur = ramp_to(TARGET_REV, cur)
        print("[3/3] 停止 0")
        ramp_to(TARGET_STOP, cur)
    except KeyboardInterrupt:
        print("\n用户中断")
    finally:
        uart.write(mdc_lite.stop())       # 急停：四通道全零
        print("已发送停止指令 (0x31 all=0)")


main()
