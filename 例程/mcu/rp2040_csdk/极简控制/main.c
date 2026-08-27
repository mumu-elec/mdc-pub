/*
 * ============================================================================
 *  极简控制（send-only）—— RP2040 C SDK 最小示例
 * ============================================================================
 *  演示用 mdc_lite（极简调用库）打包并发送控制帧：
 *    0x31 MOTOR_CTRL / 0x40 SUBSCRIBE / 0x41 UNSUBSCRIBE。
 *  只发送，不解析任何回包（若需回读转速，请用 ../控制+回调/）。
 *
 *  本文件不 include 任何 pico-sdk 头文件 —— 串口收发在真实工程里把 user_send()
 *  的实现替换为你用 hardware/uart.h 的 uart_write_blocking 即可。
 *
 *  库文件（来自 mdc_lib/rp2040/c-sdk/）：mdc_lib.h/.c、mdc_lite.h/.c
 *  库：本目录 CMakeLists 已把 mdc_lib.c + mdc_lite.c + mdc_lite_ctrl.c 打包为 mdc_lib 静态库。
 *
 *  编译验证：gcc -std=c99 -Wall -Wextra -c main.c -I<mdc_lib/rp2040/c-sdk>
 *
 *  接线见上一级 README.md（UART0_TX→RC RX、UART0_RX←RC TX、GND 共地）。
 * ============================================================================
 */
#include "mdc_lite.h"

/* 串口发送由你实现。真实工程替换为：
 *   uart_write_blocking(uart0, buf, n);   （已用 uart_init(uart0, 2000000) 初始化） */
static void user_send(const uint8_t* buf, uint16_t n)
{
    (void)buf;
    (void)n;
}

int main(void)
{
    uint8_t  buf[32];
    uint16_t n;

    /* 先订阅状态上报（如需回读转速；仅发送控制帧可省略）。建议 ≥20ms。 */
    n = md_lite_subscribe(50, buf, sizeof(buf));
    user_send(buf, n);

    /* 主循环：每 50ms 发一帧 0x31 四通道控制帧（规范 §3.3 需连续发送）。
     * 目标值含义随各通道模式：open=PWM(±1000)、speed=RPM、pos=0.1°。 */
    for (;;) {
        n = md_lite_ctrl(100, -200, 0, 300, buf, sizeof(buf));
        user_send(buf, n);

        /* 真实工程：sleep_ms(50); */
    }

    /* 退出/急停：发送全零控制帧 + 取消订阅（善后） */
    /* md_lite_stop(buf, sizeof(buf));            user_send(buf, n);      */
    /* n = md_lite_unsubscribe(buf, sizeof(buf)); user_send(buf, n);      */
    /* return 0; */
}
