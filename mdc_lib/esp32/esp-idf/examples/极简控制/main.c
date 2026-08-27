/*
 * ============================================================================
 *  极简控制（send-only）—— ESP32 ESP-IDF 最小示例
 * ============================================================================
 *  演示用 mdc_lite（极简调用库）打包并发送控制帧：
 *    0x31 MOTOR_CTRL / 0x40 SUBSCRIBE / 0x41 UNSUBSCRIBE。
 *  只发送，不解析任何回包（若需回读转速，请用 ../控制+回调/）。
 *
 *  本文件不 include 任何 IDF 头文件 —— 串口收发在真实工程里把 user_send()
 *  的实现替换为你用 driver/uart.h 的 uart_write_bytes 即可。
 *
 *  库文件（来自 mdc_lib/esp32/esp-idf/）：mdc_lib.h/.c、mdc_lite.h/.c
 *  组件：把 mdc_lib 放到 components/ 下（CMakeLists 已含 mdc_lite.c）。
 *
 *  编译验证：gcc -std=c99 -Wall -Wextra -c main.c -I<mdc_lib/esp32/esp-idf>
 *
 *  接线见上一级 README.md（UART1_TX→RC RX、UART1_RX←RC TX、GND 共地）。
 * ============================================================================
 */
#include "mdc_lite.h"

/* 串口发送由你实现。真实工程替换为：
 *   uart_write_bytes(MDC_UART, (const char*)buf, n);   （MDC_UART 为你的 UART_NUM_x） */
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

        /* 真实工程：vTaskDelay(pdMS_TO_TICKS(50)); */
    }

    /* 退出/急停：发送全零控制帧 + 取消订阅（善后） */
    /* md_lite_stop(buf, sizeof(buf));            user_send(buf, n);      */
    /* n = md_lite_unsubscribe(buf, sizeof(buf)); user_send(buf, n);      */
    /* return 0; */
}
