# -*- coding: utf-8 -*-
"""
04_chassis_control.py — 底盘模式：配置 + 整车 (vx, vy, ω) 向量控制

对应《技术手册》§4.8（底盘功能）/ §5.2（/chassis 指令）/ §5.5（0x31 底盘语义）。

底盘模式下 MOTOR_CTRL (0x31) 的 4 个 int32 含义（§5.5）：
    p1 = vx  前进速度 (mm/s)
    p2 = vy  平移速度 (mm/s)   差速底盘 vy 无效
    p3 = ω   旋转角速度 (0.001 rad/s)，如 500 = 0.5 rad/s
    p4 = 保留（发 0）

安全须知（§6.6）：
- 首次联调务必架空底盘（轮子离地）；
- 减速比未配置时实车速度会慢 g 倍——先按 /chassis 的 GEAR 行提示配好 /posangle；
- 开环档需要先标定 k_ch（/sbusparam），否则非零目标会饱和到满 PWM（GEAR/MODE 行有提示）。
"""

import time

from mdc import Mdc

SERIAL_PORT = "COM5"        # ← 改成你的 CH340 端口

# 底盘参数（按你的实车修改）
CHASSIS_TYPE = 1            # 1=差速2WD 2=差速4WD 3=麦轮O 4=全向3WD 5=全向4WD-X 6=十字
WHEEL_MM = 65               # 轮径 mm
GEO_TRACK_MM = 200          # 差速=轮距；麦轮=轴距；全向=半径（详见手册 §4.8）
GEO_WHEELBASE_MM = 150      # 差速=轴距；麦轮=轮距；全向构型此项填 0
VX_MAX = 300                # vx 限幅 mm/s（一份值三用：遥控满偏/指令限幅/斜坡上限）
W_MAX = 1000                # ω 限幅，单位 0.001rad/s（= 1.0 rad/s）
ACCEL = 800                 # 加速度 mm/s²


def main():
    mdc = Mdc(SERIAL_PORT)
    try:
        # ---------- 1) 底盘配置（RAM 生效，最后统一 /save） ----------
        mdc.cmd("/chassis type %d" % CHASSIS_TYPE)
        mdc.cmd("/chassis wheel %d" % WHEEL_MM)
        mdc.cmd("/chassis geo 0 %d" % GEO_TRACK_MM)
        mdc.cmd("/chassis geo 1 %d" % GEO_WHEELBASE_MM)
        mdc.cmd("/chassis max 0 %d" % VX_MAX)
        mdc.cmd("/chassis max 2 %d" % W_MAX)
        mdc.cmd("/chassis accel 0 %d" % ACCEL)
        mdc.cmd("/chassis stall 500")       # 失速判定 500ms
        mdc.cmd("/timeout 1000")            # 1s 无指令自动归零（安全）
        print("---- /chassis 查询 ----")
        print(mdc.cmd("/chassis"))
        # 留意输出：GEAR 行（减速比未配则车速慢 g 倍）与 MODE 行（k_ch 未标定提示）

        input("\n确认轮子已架空后回车开始运动 > ")

        # ---------- 2) 整车运动（斜坡加减速由 accel 参数限制） ----------
        print("前进 200mm/s ...")
        mdc.motor_ctrl(200, 0, 0, 0)
        time.sleep(2.0)

        print("原地旋转 0.5rad/s ...")
        mdc.motor_ctrl(0, 0, 500, 0)
        time.sleep(2.0)

        print("斜坡停车 ...")
        mdc.motor_ctrl(0, 0, 0, 0)
        time.sleep(1.0)
        mdc.cmd("/chassis stop")            # 保险：整车斜坡归零

        # ---------- 3) 持久化 ----------
        mdc.save()
        print("底盘参数已保存到 EEPROM（脱机/遥控可用，见手册 §6.5）。")
    finally:
        mdc.close()


if __name__ == "__main__":
    main()
