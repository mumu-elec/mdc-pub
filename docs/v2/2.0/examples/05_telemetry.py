# -*- coding: utf-8 -*-
"""
05_telemetry.py — 订阅实时遥测：SUBSCRIBE (0x40) → STATUS_REPORT (0xF0)

对应《技术手册》§5.6（0xF0 帧格式）。

payload 两种布局：
- 常规 56B（DEBUG_SPEED=0）: [enc:16B][tgt:16B][rpm:16B][帧计数:4B][校验计数:4B]
- 扩展 72B（DEBUG_SPEED=1）: 上面基础上在 rpm 后插入 [rpm_raw:16B]（滤波前原始 RPM）

注意：订阅后 0xF0 帧会持续到达，与文本回复共线——先做配置、后订阅；
退出时 UNSUBSCRIBE。rpm 恒为输入轴（电机轴）RPM（§5.6）。
"""

import time

from mdc import Mdc, CMD_STATUS_REPORT, parse_status

SERIAL_PORT = "COM5"        # ← 改成你的 CH340 端口
INTERVAL_MS = 50            # 推送周期（最低 20ms）
DURATION_S = 10             # 采集时长


def main():
    mdc = Mdc(SERIAL_PORT)
    try:
        mdc.subscribe(INTERVAL_MS)
        print("已订阅，每 %dms 一帧，采集 %ds（Ctrl+C 提前退出）..." % (INTERVAL_MS, DURATION_S))

        n = 0
        deadline = time.time() + DURATION_S
        try:
            while time.time() < deadline:
                r = mdc.read_frame(timeout=1.0)
                if r is None:
                    continue
                cmd, data = r
                if cmd != CMD_STATUS_REPORT:
                    continue                      # 其他帧（0xF1/0xF2 等）忽略
                st = parse_status(data)
                n += 1
                print("rpm=%s tgt=%s%s" % (
                    str(tuple(st["rpm"])),
                    str(tuple("%.1f" % t for t in st["tgt"])),
                    "  [扩展帧, rpm_raw=%s]" % str(tuple(st["rpm_raw"])) if st["raw"] else ""))
        except KeyboardInterrupt:
            pass
        print("\n共收到 %d 帧；退订..." % n)
        mdc.unsubscribe()
    finally:
        mdc.close()


if __name__ == "__main__":
    main()
