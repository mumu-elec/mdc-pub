# -*- coding: utf-8 -*-
"""
04_status_monitor — 状态订阅监控例程（ESP32 + MicroPython，基于 mdc_lib）

流程：
  1. md_bin_subscribe(50) 打包 0x40 SUBSCRIBE 帧发送，开启 50ms 周期状态上报
  2. 循环读 UART 字节逐字节喂给 MDParser；收到 0xF0 STATUS_REPORT 帧时
     用 md_parse_status() 解析（56B 常规 / 72B 扩展按长度自动兼容）并打印 enc / rpm
  3. 可选：检测到 SSD1306 OLED 时在屏上显示通道 1 RPM（try/except 包裹，无 OLED 自动跳过）

串口收发留在本例程（用户侧代码）；协议打包/解析一律调用 mdc_lib：
  - md_bin_subscribe(50)    -> 0x40 帧（interval_ms=50，固件钳位 >= 20ms）
  - md_bin_debug_speed(True)-> 0x44 帧（可选：开启 72B 扩展上报）
  - MDParser.feed(byte)     -> 完整帧返回 (cmd, payload)，否则 None
  - md_parse_status(payload)-> md_status_t(enc, tgt, rpm, rpm_raw, sbus_frame_cnt, sbus_ok_cnt, extended)
  - md_bin_unsubscribe()    -> 0x41 帧（退出时发送，关闭上报）

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
SUBSCRIBE_MS = 50      # 上报周期（协议要求 >= 20ms）
PRINT_INTERVAL_MS = 1000  # 控制台打印间隔（1 秒）
ENABLE_EXTENDED = False   # True: 先发 0x44 DEBUG_SPEED=1 开启 72B 扩展上报
RX_TIMEOUT_MS = 2000      # 读帧超时（ms）

# OLED（可选，无 SSD1306 驱动模块时自动跳过）
OLED_SCL = 22
OLED_SDA = 21
OLED_ADDR = 0x3C
# ------------------------------------------------------------

uart = UART(UART_ID, baudrate=BAUD, tx=Pin(TX_PIN), rx=Pin(RX_PIN),
            rxbuf=RXBUF, timeout=RX_TIMEOUT_MS)


def try_init_oled(scl=OLED_SCL, sda=OLED_SDA, addr=OLED_ADDR):
    """尝试初始化 SSD1306 OLED（I2C）。失败返回 None，不阻塞主流程。"""
    try:
        import ssd1306
        from machine import I2C
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
    oled = try_init_oled()

    if ENABLE_EXTENDED:
        print("开启扩展上报 (0x44 DEBUG_SPEED=1)")
        uart.write(mdc_lib.md_bin_debug_speed(True))
        utime.sleep_ms(100)          # 等 ACK 帧到达并丢弃（本例不校验）

    print("订阅状态上报 (0x40, %d ms)" % SUBSCRIBE_MS)
    uart.write(mdc_lib.md_bin_subscribe(SUBSCRIBE_MS))

    parser = mdc_lib.MDParser()
    print("等待 0xF0 上报帧...（Ctrl+C 退出）")
    last_print = 0
    try:
        while True:
            n = uart.any()
            if not n:
                utime.sleep_ms(2)
                continue
            for b in uart.read(n):
                r = parser.feed(b)                 # 流式解析：完整帧返回 (cmd, payload)
                if not r:
                    continue
                cmd, payload = r
                if cmd != mdc_lib.MD_CMD_STATUS_REPORT:   # 0xF0
                    print("收到其他帧: 0x%02X (%d B)" % (cmd, len(payload)))
                    continue
                st = mdc_lib.md_parse_status(payload)     # 56B/72B 自动兼容
                now = utime.ticks_ms()
                if utime.ticks_diff(now, last_print) >= PRINT_INTERVAL_MS:
                    print("enc  %+8d %+8d %+8d %+8d" % tuple(st.enc))
                    print("rpm  %+8d %+8d %+8d %+8d" % tuple(st.rpm))
                    if st.extended:                       # 72B 扩展模式才有 rpm_raw
                        print("raw  %+8d %+8d %+8d %+8d" % tuple(st.rpm_raw))
                    print("sbus_frame_cnt=%d  ok_cnt=%d" % (st.sbus_frame_cnt, st.sbus_ok_cnt))
                    print("-" * 44)
                    last_print = now
                    if oled is not None:
                        oled_show(oled, st.rpm[0])
    except KeyboardInterrupt:
        print("\n用户中断")
    finally:
        print("关闭上报 (0x41 UNSUBSCRIBE)")
        uart.write(mdc_lib.md_bin_unsubscribe())


main()
