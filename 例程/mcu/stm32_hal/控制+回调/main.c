/*
 * ============================================================================
 *  控制 + 回调（control + speed callback）—— STM32 HAL 最小示例
 * ============================================================================
 *  演示用 mdc_lite_ctrl（极简调用库）既发送控制帧，又流式接收下位机主动推送的
 *  0xF0 STATUS_REPORT，并把四通道 rpm 通过回调交给用户。
 *    发送：0x31 MOTOR_CTRL / 0x40 SUBSCRIBE / 0x41 UNSUBSCRIBE（复用 mdc_lite）
 *    接收：0xF0 状态上报 -> md_lite_ctrl_feed() -> on_speed(rpm[4])
 *
 *  本文件不 include 任何 HAL 头文件 —— 串口收发在真实工程里替换
 *  user_send() / user_rx_poll() 的实现即可。
 *
 *  库文件（来自 mdc_lib/stm32/hal/）：mdc_lib.h/.c、mdc_lite.h/.c、mdc_lite_ctrl.h/.c
 *  集成：把 mdc_lib.c + mdc_lite.c + mdc_lite_ctrl.c 加入工程，
 *        Include Paths 指向 mdc_lib/stm32/hal/。
 *
 *  编译验证：gcc -std=c99 -Wall -Wextra -c main.c -I<mdc_lib/stm32/hal>
 *
 *  接线见上一级 README.md（PA2→RC RX、PA3←RC TX、GND 共地）。
 * ============================================================================
 */
#include "mdc_lite_ctrl.h"

/* 串口发送由你实现。真实工程替换为：
 *   HAL_UART_Transmit(&huart2, buf, n, 100); */
static void user_send(const uint8_t* buf, uint16_t n)
{
    (void)buf;
    (void)n;
}

/* 0xF0 速度回调：四通道实时转速 rpm[0..3]（int32）。
 * 真实工程里可在此更新闭环目标 / 经串口或缓冲区上报上位机。 */
static void on_speed(const int32_t rpm[4])
{
    (void)rpm;
}

/* 串口接收：每收到一字节就喂给 mdc_lite_ctrl 接收器。
 * 真实工程在 USART 接收中断里从硬件读一字节后调用 md_lite_ctrl_feed(c, byte)。 */
static void user_rx_poll(md_lite_ctrl_t* c)
{
    (void)c;
    /* 真实工程：
     *   uint8_t b;
     *   if (HAL_UART_Receive(&huart2, &b, 1, 0) == HAL_OK)
     *       md_lite_ctrl_feed(c, b);
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

        user_rx_poll(&ctrl);       /* 也可在 USART 接收中断里直接喂 */

        /* 真实工程：HAL_Delay(50); */
    }
    /* return 0; */
}
