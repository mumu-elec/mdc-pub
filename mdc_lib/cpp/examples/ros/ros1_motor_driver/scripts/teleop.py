#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Motor Driver Controller — ROS1 (Noetic) 键盘遥控节点
====================================================
按键:
  1~4     选择通道 (Motor A~D)
  + / =   当前通道目标值 +step
  - / _   当前通道目标值 -step
  0       全部通道归零
  q       退出 (自动发送归零)

说明:
  * 以 10Hz (参数 ~rate) 周期发布 /motor_cmd, 保持控制链路活跃,
    避免固件 /timeout 超时保护归零。
  * 目标值含义随各通道控制模式: 开环=PWM±1000 / 速度=RPM / 位置=0.1°。
  * 驱动节点内部按 30Hz 限流发送 0x31 控制帧。

运行:
  rosrun ros1_motor_driver teleop.py
  rosrun ros1_motor_driver teleop.py _step:=500 _rate:=20.0
  或直接: python3 scripts/teleop.py
"""
import select
import sys
import termios
import tty

import rospy
from ros1_motor_driver.msg import MotorCmd


class Teleop:
    def __init__(self):
        rospy.init_node('motor_teleop')
        self.step = rospy.get_param('~step', 100)      # 每次按键的调整量
        self.rate_hz = rospy.get_param('~rate', 10.0)  # /motor_cmd 发布频率 (Hz)

        self.pub = rospy.Publisher('/motor_cmd', MotorCmd, queue_size=10)

        self.targets = [0, 0, 0, 0]
        self.ch = 1            # 当前选中通道 1~4
        self.quit = False

        self._fd = sys.stdin.fileno()
        self._old_attr = termios.tcgetattr(self._fd)
        tty.setcbreak(self._fd)          # 逐字符读取, 无需回车
        print(self.help_text(), flush=True)

    def help_text(self):
        return (
            "\n===== Motor Driver Teleop (ROS1) ====="
            "\n  1~4     选择通道"
            "\n  + / =   目标值 +%d"
            "\n  - / _   目标值 -%d"
            "\n  0       全部通道归零"
            "\n  q       退出 (自动归零)"
            "\n  当前通道: %d    targets: %s"
            "\n====================================="
        ) % (self.step, self.step, self.ch, self.targets)

    def _read_key(self):
        rlist, _, _ = select.select([sys.stdin], [], [], 0)
        return sys.stdin.read(1) if rlist else ''

    def tick(self):
        key = self._read_key()
        if key:
            if key in '1234':
                self.ch = int(key)
            elif key in ('+', '='):
                self.targets[self.ch - 1] += self.step
            elif key in ('-', '_'):
                self.targets[self.ch - 1] -= self.step
            elif key == '0':
                self.targets = [0, 0, 0, 0]
            elif key == 'q':
                self.targets = [0, 0, 0, 0]
                self.quit = True
            print('ch=%d targets=%s' % (self.ch, self.targets), flush=True)
        self.publish()

    def publish(self):
        msg = MotorCmd()
        msg.target = list(self.targets)
        self.pub.publish(msg)

    def run(self):
        rate = rospy.Rate(self.rate_hz)
        try:
            while not rospy.is_shutdown() and not self.quit:
                self.tick()
                rate.sleep()
        finally:
            # 退出前发送一次归零, 并恢复终端
            msg = MotorCmd()
            msg.target = [0, 0, 0, 0]
            self.pub.publish(msg)
            termios.tcsetattr(self._fd, termios.TCSADRAIN, self._old_attr)


if __name__ == '__main__':
    Teleop().run()
