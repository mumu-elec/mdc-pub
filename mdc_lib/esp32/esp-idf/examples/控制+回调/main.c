/*
 * ============================================================================
 *  控制 + 回调（control + speed callback）—— ESP32 ESP-IDF 最小示例
 * ============================================================================
 *  演示用 mdc_lite_ctrl（极简调用库）既发送控制帧，又流式接收下位机主动推送的
 *  0xF0 STATUS_REPORT，并把四通道 rpm 通过回调交给用户。
 *    发送：0x31 MOTOR_CTRL / 0x40 SUBSCRIBE / 0x41 UNSUBSCRIBE（复用 mdc_lite）
 *    接收：0xF0 状态上报 -> md_lite_ctrl_feed() -> on_speed(rpm[4])
 *
 *  本文件不 include 任何 IDF 头文件 —— 串口收发在真实工程里替换 user_send()
 *  与 UART 事件循环（用 driver/uart.h 读字节后喂 md_lite_ctrl_feed）即可。
 *
 *  库文件（来自 mdc_lib/esp32/esp-idf/）：mdc_lib.h/.c、mdc_lite.h/.c、mdc_lite_ctrl.h/.c
 *  组件：把 mdc_lib 放到 components/ 下（CMakeLists 已含 mdc_lite.c / mdc_lite_ctrl.c）。
 *
 *  编译验证：gcc -std=c99 -Wall -Wextra -c main.c -I<mdc_lib/esp32/esp-idf>
 *
 *  接线见上一级 README.md（UART1_TX→RC RX、UART1_RX←RC TX、GND 共地）。
 * ============================================================================
 */
#include "mdc_lite_ctrl.h"

/* 串口发送由你实现。真实工程替换为：
 *   uart_write_bytes(MDC_UART, (const char*)buf, n); */
static void user_send(const uint8_t* buf, uint16_t n)
{
    (void)buf;
    (void)n;
}

/* 0xF0 速度回调：四通道实时转速 rpm[0..3]（int32）。 */
static void on_speed(const int32_t rpm[4])
{
    (void)rpm;
}

/* 串口接收：每收到一字节喂给 mdc_lite_ctrl 接收器。
 * 真实工程在 UART 事件循环（UART_DATA）里 uart_read_bytes 后逐字节喂入。 */
static void user_rx_poll(md_lite_ctrl_t* c)
{
    (void)c;
    /* 真实工程：
     *   uint8_t data[128];
     *   int n = uart_read_bytes(MDC_UART, data, sizeof(data), pdMS_TO_TICKS(10));
     *   for (int i = 0; i < n; i++) md_lite_ctrl_feed(c, data[i]);
     */
}

int main(void)
{
    static md_lite_ctrl_t ctrl;    /* 解析器 + 回调（静态区，避免占栈） */
    uint8_t  buf[32];
    uint16_t n;

    /* 注册速度回调并初始化流式解析器 */
    md_lite_ctrl_init(&ctrl, on_speed);

    /* 先订阅状态上报：收 0xF0 回调的前提（建议 ≥20ms） */
    n = md_lite_subscribe(50, buf, sizeof(buf));
    user_send(buf, n);

    /* 主循环：周期发控制帧 + 轮询喂接收字节（0xF0 完整帧到达自动回调） */
    for (;;) {
        n = md_lite_ctrl(100, 0, 0, 0, buf, sizeof(buf));
        user_send(buf, n);

        user_rx_poll(&ctrl);       /* 也可在 UART 事件循环里直接喂 */

        /* 真实工程：vTaskDelay(pdMS_TO_TICKS(50)); */
    }
    /* return 0; */
}
