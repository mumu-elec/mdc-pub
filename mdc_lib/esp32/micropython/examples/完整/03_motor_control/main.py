# -*- coding: utf-8 -*-
"""
03_motor_control — 实时电机控制例程（ESP32 + MicroPython，基于 mdc_lib）

流程：
  1. 演示序列：开环正转 -> 反转 -> 停止（每档 PHASE_MS，斜坡平滑过渡）
  2. 交互模式：REPL 输入目标值（如 400 / -200 / 0），q 退出

要点：
  - 控制帧为 0x31 MOTOR_CTRL，由 mdc_lib.md_bin_motor_ctrl(t0, t1, t2, t3) 打包
    （四通道 int32 LE，本例只动通道 1，其余通道填 0）
  - 斜坡：每个发送周期（PERIOD_MS=50ms）向目标靠近固定步长 STEP，同时持续发送保持控制权
  - 目标值含义取决于通道控制模式（见 README）：
      开环 = PWM（±1000）；速度闭环 = RPM；位置闭环 = 0.1°（±3600 = ±360.0°）
  - 速度/位置闭环需先用文本指令配好 CPR 与 PID（/cpr、/speedctrl、/posctrl、/mode）
  - 控制仲裁：默认 /priority 0 = USART2 优先，本板（USART2 侧）控制帧天然生效

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
RXBUF = 1024           # UART 接收缓冲区大小（字节）
CH = 0                 # 通道索引 0~3，对应 Motor A~D（本例只控制通道 1）
STEP = 50              # 斜坡步长：每个周期向目标靠近 50（PWM 单位）
PERIOD_MS = 50         # 0x31 发送周期（协议建议 30/50/100ms）
TARGET_FWD = 300       # 开环正转目标 PWM（+300/1000）
TARGET_REV = -300      # 反转目标 PWM
TARGET_STOP = 0        # 停止
PHASE_MS = 2000        # 演示序列每档持续时间
HOLD_MS = 3000         # 交互模式到达目标后的保持时间（ms）
# ------------------------------------------------------------

uart = UART(UART_ID, baudrate=BAUD, tx=Pin(TX_PIN), rx=Pin(RX_PIN),
            rxbuf=RXBUF)


def send_ctrl(current):
    """md_bin_motor_ctrl() 打包 0x31 帧并发送（只动 CH 通道，其余填 0）。"""
    t = [0, 0, 0, 0]
    t[CH] = current
    uart.write(mdc_lib.md_bin_motor_ctrl(t[0], t[1], t[2], t[3]))


def ramp_to(target, current, phase_ms):
    """从 current 斜坡逼近 target，每 PERIOD_MS 发送一次 0x31。

    到达 target 后继续发送保持（防止 /timeout 超时归零），持续 phase_ms 后返回。
    返回当前目标值 current。
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
        if utime.ticks_diff(utime.ticks_ms(), start) >= phase_ms:
            break
    return current


def demo_sequence():
    """演示序列：开环正转 -> 反转 -> 停止，每档 PHASE_MS。"""
    print("== 演示序列（开环 PWM，通道 %d，每档 %d ms）==" % (CH + 1, PHASE_MS))
    cur = 0
    print("[1/3] 开环正转 +%d" % TARGET_FWD)
    cur = ramp_to(TARGET_FWD, cur, PHASE_MS)
    print("[2/3] 反转 %d" % TARGET_REV)
    cur = ramp_to(TARGET_REV, cur, PHASE_MS)
    print("[3/3] 停止 0")
    cur = ramp_to(TARGET_STOP, cur, PHASE_MS)
    return cur


def interactive(cur):
    """交互模式：REPL 输入目标值 -> 斜坡过渡并保持 HOLD_MS -> 再次询问。"""
    print("== 交互模式 ==")
    print("输入目标值（开环 PWM：-1000~1000），q 退出；到达后保持 %d ms" % HOLD_MS)
    while True:
        try:
            s = input("target> ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            break
        if s in ("q", "quit", "exit"):
            break
        try:
            tgt = int(s)
        except ValueError:
            print("请输入整数目标值（或 q 退出）")
            continue
        cur = ramp_to(tgt, cur, HOLD_MS)


def main():
    try:
        cur = demo_sequence()
        interactive(cur)
    except KeyboardInterrupt:
        print("\n用户中断")
    finally:
        # 安全收尾：停止所有电机
        send_ctrl(0)
        print("已发送停止指令 (0x31 all=0)")


main()
