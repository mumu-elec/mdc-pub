/*
 * ============================================================================
 *  main_example.c — STM32 HAL 主循环集成示例（F103C8T6，F4 用法相同）
 * ============================================================================
 *  本文件不是完整工程，而是演示如何把 mdc_lib（通用调用库）集成进
 *  CubeMX 生成的 main.c。把下面的关键片段合并进你的 main.c 即可。
 *
 *  【CubeMX 需要配置的内容】
 *    1. USART2（PA2=TX、PA3=RX）：115200-8N1，使能 NVIC
 *       （USART2 global interrupt），其余默认。
 *    2. 时钟：HSE 8MHz → PLL → SYSCLK 72MHz（F1 蓝板常见配置）。
 *    3. 建议使能 1ms 时基（HAL_GetTick 默认 SysTick 1ms，无需额外配置）。
 *
 *  【需要加入工程的源文件】
 *    - mdc_lib/stm32/hal/mdc_lib.h   → 工程 Core/Inc/（加入 Include Paths）
 *    - mdc_lib/stm32/hal/mdc_lib.c   → 工程 Core/Src/（加入源文件组）
 *
 *  【接线】
 *    STM32 PA2 (USART2_TX) → 控制板 RC 信号（RC RX）
 *    STM32 PA3 (USART2_RX) ← 控制板 RC 信号（RC TX）
 *    GND 共地
 *
 *  【控制板预配置】（USB 串口 2000000-8N1 连接后执行）
 *    /uart2 115200 0 uart
 *    默认控制优先级 USART2 优先（/priority 0）→ 本例程 0x31 控制帧天然生效。
 *
 *  协议层全部由 mdc_lib 完成（打包/解析），本文件只负责串口收发：
 *    - 发送：md_bin_* / md_text_* 返回「要发送的字节」，交给 HAL_UART_Transmit
 *    - 接收：USART2 接收中断逐字节喂 md_parser_feed()（自动找 0xAA + CRC 校验），
 *            收到 0xF0 用 md_parse_status() 解析；ACK 用 md_parse_ack() 解析
 *
 *  例程行为：
 *    - 上电用 md_bin_subscribe(100) 订阅状态上报
 *    - 主循环每 50ms 用 md_bin_motor_ctrl 发送一帧 0x31
 *      （ch1=200、ch2=-200、ch3/ch4=0，单位取决于各通道模式）
 *    - USART2 接收中断逐字节喂 md_parser_feed，0xF0 解析结果在主循环打印
 * ============================================================================
 */
#include "main.h"                /* CubeMX 生成 */
#include "mdc_lib.h"
#include <stdio.h>

extern UART_HandleTypeDef huart2;

static md_parser_t g_parser;     /* 流式解析器状态（全局/静态区，避免占栈） */
static uint8_t     s_rx_byte;    /* 单字节接收缓冲 */

/* 0xF0 / ACK 解析结果（中断写入，主循环打印，避免在中断里做串口输出） */
static md_status_t g_status;     /* 最近一次 0xF0 解析结果 */
static md_ack_t    g_ack;        /* 最近一次 ACK 解析结果 */
static uint8_t     g_frame_kind; /* 0=无新帧 1=0xF0 2=ACK */

/* ============================================================================
 * 发送辅助：把 mdc_lib 打包结果交给 USART2（用户侧收发）
 * ==========================================================================*/
static void uart2_send(const uint8_t *buf, uint16_t len)
{
    HAL_UART_Transmit(&huart2, buf, len, 100);   /* 100ms 超时 */
}

/* 简单 printf 重定向（F1/F4 常见做法：勾选 MicroLIB 后重写 fputc）。
 * 若你的工程已有 printf 重定向，删除本函数即可。 */
#if defined(__CC_ARM) || defined(__ARMCC_VERSION)
int fputc(int ch, FILE *f)
{
    (void)f;
    HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, 100);
    return ch;
}
#endif

/* ============================================================================
 * 主函数示例：合并进 CubeMX 生成的 main()。
 * ==========================================================================*/
int main(void)
{
    uint8_t  buf[32];
    uint16_t n;
    uint32_t last_send;

    /* ---- CubeMX 生成的初始化（保持原样） ---- */
    HAL_Init();
    SystemClock_Config();          /* CubeMX 生成：72MHz */
    MX_GPIO_Init();
    MX_USART2_UART_Init();         /* 115200-8N1，NVIC 已使能 */

    /* ---- 应用初始化 ---- */
    md_parser_init(&g_parser);

    /* 订阅状态上报：每 100ms 推送一帧 0xF0（控制板确认后回 ACK） */
    n = md_bin_subscribe(100, buf, sizeof(buf));
    uart2_send(buf, n);

    /* 可选：把 ch1/ch2 切到速度闭环（文本指令由 mdc_lib 构造，自动补 '\n'）：
     *   char line[32];
     *   n = md_text_mode(1, "speed", line, sizeof(line));
     *   uart2_send((const uint8_t *)line, n);
     *   n = md_text_mode(2, "speed", line, sizeof(line));
     *   uart2_send((const uint8_t *)line, n);
     * 也可用 md_text_build 构造任意文本指令：
     *   n = md_text_build("/speedctrl", "1 0.5 0.02 0.01", line, sizeof(line));
     */

    /* 启动 USART2 单字节接收中断（配合下方 HAL_UART_RxCpltCallback） */
    HAL_UART_Receive_IT(&huart2, &s_rx_byte, 1);

    /* ---- 主循环：每 50ms 发送一帧 0x31 控制帧（规范 §3.3 需连续发送） ---- */
    last_send = HAL_GetTick();

    while (1)
    {
        if (HAL_GetTick() - last_send >= 50) {
            /* 0x31 MOTOR_CTRL：mdc_lib 打包整帧（含 0xAA 同步字 + CRC8）。
             * 目标值含义取决于各通道模式：开环=PWM(±1000)、速度=RPM、位置=0.1° */
            n = md_bin_motor_ctrl(200, -200, 0, 0, buf, sizeof(buf));
            uart2_send(buf, n);
            last_send = HAL_GetTick();
        }

        /* 打印中断里解析好的 0xF0 / ACK（打印走 USART2，可与控制帧共用一根线） */
        if (g_frame_kind != 0) {
            if (g_frame_kind == 1) {
                printf("0xF0 enc=%ld,%ld,%ld,%ld tgt=%.1f,%.1f,%.1f,%.1f rpm=%ld,%ld,%ld,%ld\r\n",
                       (long)g_status.enc[0], (long)g_status.enc[1],
                       (long)g_status.enc[2], (long)g_status.enc[3],
                       (double)g_status.tgt[0], (double)g_status.tgt[1],
                       (double)g_status.tgt[2], (double)g_status.tgt[3],
                       (long)g_status.rpm[0], (long)g_status.rpm[1],
                       (long)g_status.rpm[2], (long)g_status.rpm[3]);
            } else {
                printf("ACK cmd=0x%02X err=0x%02X\r\n",
                       (unsigned)g_ack.cmd, (unsigned)g_ack.err);
            }
            g_frame_kind = 0;
        }

        /* 其它业务逻辑放这里 */
    }
}

/* ============================================================================
 * USART2 接收中断回调：每收到 1 字节喂给 mdc_lib 流式解析器。
 * （CubeMX 生成的 stm32f1xx_it.c 中已调用 HAL_UART_IRQHandler，
 *   本回调由 HAL 在中断上下文调用。）
 * ==========================================================================*/
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    uint8_t cmd;
    const uint8_t *payload;
    uint16_t plen;

    if (huart->Instance == USART2) {
        /* 流式解析：自动找 0xAA 同步 + CRC 校验，完整帧返回 1 */
        if (md_parser_feed(&g_parser, s_rx_byte, &cmd, &payload, &plen)) {
            if (cmd == MD_CMD_STATUS_REPORT) {          /* 0xF0 状态上报 */
                /* payload 指向解析器内部缓冲，必须在下次 feed 前消费：
                 * 这里立即解析进 g_status，主循环负责打印 */
                if (md_parse_status(payload, plen, &g_status)) {
                    g_frame_kind = 1;
                }
            } else if (plen == 1) {                     /* ACK 帧（0x40 应答等） */
                if (md_parse_ack(payload, plen, &g_ack)) {
                    g_frame_kind = 2;
                }
            }
            /* 其它上报帧（0xF1/0xF2 等）按需扩展 */
        }
        HAL_UART_Receive_IT(&huart2, &s_rx_byte, 1);   /* 重新挂接接收 */
    }
}
