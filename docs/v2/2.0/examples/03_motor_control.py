# -*- coding: utf-8 -*-
"""
03_motor_control.py — 单电机三种控制模式：开环 / 速度闭环 / 位置闭环

对应《技术手册》§4.1（控制模式）/ §5.5（MOTOR_CTRL 0x31）。

安全须知（§6.6）：
- 首次联调请让轮子悬空（电机可自由旋转）；
- 先开启超时保护：程序异常停发指令后电机会自动归零；
- MOTOR_CTRL (0x31) 无 ACK——持续发送期间电机持续运动，停止发送即失去目标。

速度/位置闭环需要编码器：请先按电机实际规格 /cpr 设置线数。
"""

import time

from mdc import Mdc

SERIAL_PORT = "COM5"        # ← 改成你的 CH340 端口
CH = 1                      # 演示通道：1 = Motor A（内部通道 0）
ENC_CPR = 11                # ← 编码器每转线数（按规格书填，常见 11/13/500；
#                              固件内部自动 ×4 倍频；不确定时手动转一圈看 /check 计数增量）


def pause(prompt):
    input("\n[%s] 回车继续，Ctrl+C 退出 > " % prompt)


def main():
    mdc = Mdc(SERIAL_PORT)
    try:
        # 安全措施：1s 无控制指令 → 所有电机输出归零
        mdc.cmd("/timeout 1000")
        # 编码器线数（速度/位置闭环的前提；0 = 强制开环）
        mdc.cmd("/cpr %d %d" % (CH, ENC_CPR))
        print(mdc.cmd("/check"))

        # ---------- 1) 开环：目标 = PWM ±1000 ----------
        pause("开环 30%% PWM")
        mdc.cmd("/mode %d open" % CH)
        mdc.motor_ctrl(300, 0, 0, 0)          # 通道 A：+300 PWM
        time.sleep(1.0)
        mdc.motor_ctrl(0, 0, 0, 0)            # 停

        # ---------- 2) 速度闭环：目标 = RPM（输入轴/电机轴） ----------
        pause("速度闭环 300 RPM")
        mdc.cmd("/mode %d speed" % CH)
        mdc.motor_ctrl(300, 0, 0, 0)          # 通道 A：300 RPM
        time.sleep(1.5)
        print(mdc.cmd("/check"))              # 观察 RPM 是否跟上目标
        mdc.motor_ctrl(0, 0, 0, 0)

        # ---------- 3) 位置闭环：目标 = 0.1°（3600 = 一圈），级联 位置环→速度环 ----------
        pause("位置闭环 +90.0°")
        mdc.cmd("/enczero %d" % CH)           # 位置基准清零
        mdc.cmd("/mode %d pos" % CH)
        mdc.motor_ctrl(900, 0, 0, 0)          # 900 = +90.0°
        time.sleep(2.0)
        print(mdc.cmd("/check"))
        mdc.motor_ctrl(0, 0, 0, 0)

        print("\n完成。参数改动仅在 RAM（/save 持久化，见 02_params.py）。")
    finally:
        mdc.close()


if __name__ == "__main__":
    main()
