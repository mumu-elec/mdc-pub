#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
04_motor_control — 实时电机控制例程
====================================
以固定周期连续发送 0x31 MOTOR_CTRL 帧（mdc_lib.md_bin_motor_ctrl 打包，
四通道 int32 目标值），带斜坡平滑（每次发送向目标逼近 --ramp 步长）。
运行中键盘控制：
    '+' 增大目标    '-' 减小目标    '0' 目标清零    'q' 退出（退出前发送全零归零）

控制目标含义随通道控制模式（协议规范 §3.3）：
    open  = PWM（±1000）；speed = RPM；pos = 0.1°（±3600 = ±360.0°）

启动时自动发送文本指令 /priority 1（mdc_lib.md_text_build 构造）并等待回显：
USB 主控（PC）做实时控制必须获得控制仲裁优先权，否则 0x31 控制帧
可能因 USART2 优先而被拒绝（规范 §1）。

用法：
    python motor_control.py --port COM5 --mode open  --ch 1 --target 300
    python motor_control.py --mode speed --target 500 --ramp 100 --interval 50
    python motor_control.py --mode pos   --target 1800     # 180.0°

⚠️ 速度/位置闭环前需先配置编码器 CPR 与 PID（文本指令示例，见 README）：
    /cpr 1 500
    /speedctrl 1 0.5 0.02 0.01
    /posctrl 1 0.1 0.01 0.0
    /mode 1 speed
    /save
"""

import argparse
import sys
import time

import serial

# 加载本仓库 mdc_lib（正式工程：复制 mdc_lib/python/mdc_lib.py 到项目目录即可）
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "mdc_lib", "python"))
import mdc_lib

BAUDRATE = 2000000

# 各模式目标值限幅（speed 模式无限幅，由固件/PID 约束）
LIMIT = {"open": 1000, "speed": None, "pos": 3600}


def setup_console():
    """控制台输出统一 UTF-8（兼容 GBK / 输出重定向场景）。"""
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass


class KeyReader:
    """跨平台非阻塞按键读取：Windows 用 msvcrt；POSIX 用 termios cbreak。"""

    def __init__(self):
        self._fd = None
        self._old = None
        if os.name != "nt":
            import termios
            import tty
            self._fd = sys.stdin.fileno()
            self._old = termios.tcgetattr(self._fd)
            tty.setcbreak(self._fd)          # cbreak 保留 Ctrl+C (ISIG)

    def get(self):
        """有按键返回字符，无按键返回 None（非阻塞）。"""
        if os.name == "nt":
            import msvcrt
            return msvcrt.getwch() if msvcrt.kbhit() else None
        import select
        if select.select([sys.stdin], [], [], 0)[0]:
            return sys.stdin.read(1)
        return None

    def close(self):
        if self._fd is not None and self._old is not None:
            import termios
            termios.tcsetattr(self._fd, termios.TCSADRAIN, self._old)


class MotorController:
    """实时控制循环：mdc_lib 打包 0x31 帧 + 斜坡平滑 + 键盘调速。"""

    def __init__(self, port, mode, ch, target, interval_ms, ramp, step):
        self.mode = mode
        self.ch = ch - 1                     # 内部 0~3
        self.target = float(target)
        self.current = 0.0                   # 当前斜坡输出
        self.interval_s = interval_ms / 1000.0
        self.ramp = ramp
        self.step = step
        self.limit = LIMIT[mode]

        self.ser = serial.Serial(port, BAUDRATE, timeout=0.05)
        self.ser.reset_input_buffer()

    # ── 文本指令（mdc_lib 构造，用于 /priority 1）────────
    def send_text(self, line_bytes):
        """发送一条 mdc_lib 打包的文本指令（bytes，含 \\n）并等待回显。"""
        self.ser.write(line_bytes)
        echo = b""
        last = time.monotonic()
        deadline = time.monotonic() + 0.8
        while time.monotonic() < deadline:
            chunk = self.ser.read(self.ser.in_waiting or 1)
            if chunk:
                echo += chunk
                last = time.monotonic()
            elif time.monotonic() - last >= 0.1:
                break
        return echo.decode("utf-8", errors="replace")

    # ── 二进制控制帧（mdc_lib 打包 0x31）─────────────────
    def send_ctrl(self, targets):
        """发送 0x31 MOTOR_CTRL 帧：mdc_lib.md_bin_motor_ctrl 打包（4×int32 LE）。"""
        self.ser.write(mdc_lib.md_bin_motor_ctrl(*[int(v) for v in targets]))

    def clamp(self, v):
        """按模式限幅；speed 模式无限幅（由固件/PID 约束）。"""
        if self.limit is not None:
            return max(-self.limit, min(self.limit, v))
        return v

    def ramp_step(self):
        """当前值向目标逼近 ramp 步长。"""
        diff = self.target - self.current
        move = max(-self.ramp, min(self.ramp, diff))
        self.current = self.clamp(self.current + move)
        return self.current

    def run(self):
        print(f"模式={self.mode}  通道={self.ch + 1}  初始目标={self.target:g}  "
              f"周期={self.interval_s * 1000:g}ms  斜坡={self.ramp:g}/周期")
        print("按键: '+' 增大目标  '-' 减小目标  '0' 清零  'q' 退出")

        # 文本指令：USB 主控（协议规范 §1 控制优先级）
        print("\n>>> /priority 1")
        echo = self.send_text(mdc_lib.md_text_build("/priority", "1"))
        if echo.strip():
            print(echo.rstrip())
        print("说明: /priority 1 使 USB 端口获得控制仲裁优先权，"
              "否则 0x31 控制帧可能被 USART2 优先仲裁拒绝。\n")

        keys = KeyReader()
        try:
            next_tick = time.monotonic()
            while True:
                # 斜坡逼近并发送（mdc_lib 打包 0x31 帧）
                cur = self.ramp_step()
                targets = [0, 0, 0, 0]
                targets[self.ch] = int(round(cur))
                self.send_ctrl(targets)

                # 刷新一行状态
                line = (f"\rCH{self.ch + 1} [{self.mode}]  target={self.target:+.0f}  "
                        f"current={cur:+.0f}  "
                        f"{(abs(cur) / self.ramp) if self.ramp else 0:>6.1f} ramp-steps  ")
                sys.stdout.write(line)
                sys.stdout.flush()

                # 键盘输入（非阻塞）
                k = keys.get()
                if k == "+":
                    self.target = self.clamp(self.target + self.step)
                elif k == "-":
                    self.target = self.clamp(self.target - self.step)
                elif k == "0":
                    self.target = 0.0
                elif k in ("q", "Q"):
                    print("\n收到 q，正在归零退出...")
                    break

                # 节拍控制
                next_tick += self.interval_s
                sleep_s = next_tick - time.monotonic()
                if sleep_s > 0:
                    time.sleep(sleep_s)
        finally:
            keys.close()
            self.stop()

    def stop(self):
        """退出前发送全零控制帧（归零），并关闭串口。"""
        try:
            # 连发几帧确保至少一帧被固件收到（规避停止瞬间丢帧）
            for _ in range(3):
                self.send_ctrl([0, 0, 0, 0])
                time.sleep(0.05)
            print("已发送全零归零帧。")
        finally:
            if self.ser.is_open:
                self.ser.close()
                print("串口已关闭。")


def main():
    setup_console()
    parser = argparse.ArgumentParser(description="Motor Driver Controller 实时电机控制")
    parser.add_argument("--port", default=None, help="串口号，如 COM5；缺省自动选择第一个 CH340")
    parser.add_argument("--mode", choices=["open", "speed", "pos"], default="open",
                        help="控制模式: open=开环PWM / speed=速度RPM / pos=位置0.1° (默认 open)")
    parser.add_argument("--ch", type=int, default=1, choices=range(1, 5),
                        help="通道 1~4 (默认 1)")
    parser.add_argument("--target", type=float, default=0.0,
                        help="初始目标值: open=PWM±1000 / speed=RPM / pos=0.1° (默认 0)")
    parser.add_argument("--interval", type=int, default=50,
                        help="发送间隔 ms (默认 50, 常用 30/50/100)")
    parser.add_argument("--ramp", type=float, default=50,
                        help="斜坡步进/周期 (默认 50, 0=直接跳变)")
    parser.add_argument("--step", type=float, default=100,
                        help="按 +/- 键时目标增减量 (默认 100)")
    args = parser.parse_args()

    # 串口选择
    port = args.port
    if port is None:
        from serial.tools import list_ports
        cands = [p for p in list_ports.comports()
                 if p.vid == 0x1A86 or "CH340" in (p.description or "").upper()]
        if not cands:
            print("未找到 CH340 串口，请用 --port 手动指定。")
            return
        port = cands[0].device
        print(f"自动选择串口: {port}")

    try:
        ctrl = MotorController(port, args.mode, args.ch, args.target,
                               args.interval, args.ramp, args.step)
    except serial.SerialException as e:
        print(f"[错误] 串口打开失败: {e}")
        return

    try:
        ctrl.run()
    except KeyboardInterrupt:
        print("\n用户中断 (Ctrl+C)，正在归零退出...")
        ctrl.stop()


if __name__ == "__main__":
    main()
