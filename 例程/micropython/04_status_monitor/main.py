# -*- coding: utf-8 -*-
"""
04_status_monitor — 状态订阅监控例程（ESP32 + MicroPython）

流程：
  1. 订阅状态上报（0x40 SUBSCRIBE，50ms 周期）-> 控制板周期推送 0xF0 STATUS_REPORT
  2. 解析 0xF0：常规 56B（enc[4]i32 + tgt[4]f32 + rpm[4]i32 + 2×u32）与
     扩展 72B（多 rpm_raw[4]i32，需先 0x44 DEBUG_SPEED 开启）按 payload 长度兼容
  3. 控制台每秒打印 4 通道 enc / rpm（可选 OLED 显示通道 1 rpm）

运行前：控制板 USART2 需已配置为 UART 模式（/uart2 115200 0 uart）。
"""

from motor_driver import MotorDriver, CMD_STATUS_REPORT, parse_status_report
import utime

# ---------------- 用户可调参数 ----------------
SUBSCRIBE_MS = 50        # 上报周期（协议要求 >= 20ms）
PRINT_INTERVAL_MS = 1000 # 控制台打印间隔（1 秒）
ENABLE_EXTENDED = False  # True: 先发 0x44 DEBUG_SPEED=1 开启 72B 扩展上报
RX_TIMEOUT_MS = 2000     # 读帧超时（ms）

# OLED（可选，无 SSD1306 驱动模块时自动跳过）
OLED_SCL = 22
OLED_SDA = 21
OLED_ADDR = 0x3C
# ----------------------------------------------


def try_init_oled(scl=OLED_SCL, sda=OLED_SDA, addr=OLED_ADDR):
    """尝试初始化 SSD1306 OLED（I2C）。失败返回 None，不阻塞主流程。"""
    try:
        import ssd1306
        from machine import I2C, Pin
        i2c = I2C(0, scl=Pin(scl), sda=Pin(sda), freq=400000)
        oled = ssd1306.SSD1306_I2C(128, 64, i2c, addr=addr)
        oled.fill(0)
        oled.text("MDC Monitor", 0, 0)
        oled.show()
        print("OLED 初始化成功 (I2C scl=%d sda=%d addr=0x%02X)" % (scl, sda, addr))
        return oled
    except Exception as e:
        print("未检测到 SSD1306 OLED，跳过显示（%s）" % e)
        return None


def oled_show(oled, rpm1):
    """OLED 显示通道 1 的 RPM。"""
    oled.fill(0)
    oled.text("CH1 RPM", 0, 0)
    oled.text("%+d" % rpm1, 0, 16)
    oled.show()


def main():
    drv = MotorDriver()
    oled = try_init_oled()

    if ENABLE_EXTENDED:
        print("开启扩展上报 (0x44 DEBUG_SPEED=1)")
        if drv.debug_speed(True, timeout_ms=500) is not True:
            print("DEBUG_SPEED 设置失败/超时，继续按常规 56B 解析")

    print("订阅状态上报 (0x40, %d ms)" % SUBSCRIBE_MS)
    if drv.subscribe(SUBSCRIBE_MS, timeout_ms=500) is not True:
        print("SUBSCRIBE 失败/超时！请检查接线 / 共地 / 预配置后重试")
        return

    print("等待 0xF0 上报帧...（Ctrl+C 退出）")
    last_print = 0
    try:
        while True:
            res = drv.read_frame(timeout_ms=RX_TIMEOUT_MS)
            if res is None:
                print("读帧超时（%d ms 无数据）" % RX_TIMEOUT_MS)
                continue
            cmd, payload = res
            if cmd != CMD_STATUS_REPORT:
                # 其他主动上报帧（如 0xF1 DETECT_REPORT / 0xF2 SBUS_DATA），忽略
                print("收到其他帧: 0x%02X (%d B)" % (cmd, len(payload)))
                continue
            st = parse_status_report(payload)
            now = utime.ticks_ms()
            if utime.ticks_diff(now, last_print) >= PRINT_INTERVAL_MS:
                print("enc  %+8d %+8d %+8d %+8d" % tuple(st["enc"]))
                print("rpm  %+8d %+8d %+8d %+8d" % tuple(st["rpm"]))
                if st["rpm_raw"] is not None:
                    print("raw  %+8d %+8d %+8d %+8d" % tuple(st["rpm_raw"]))
                print("sbus_frame_cnt=%d  ok_cnt=%d" % (st["sbus_frame_cnt"], st["sbus_ok_cnt"]))
                print("-" * 44)
                last_print = now
                if oled is not None:
                    oled_show(oled, st["rpm"][0])
    except KeyboardInterrupt:
        print("\n用户中断")
    finally:
        print("关闭上报 (0x41 UNSUBSCRIBE)")
        drv.unsubscribe(timeout_ms=500)


main()
