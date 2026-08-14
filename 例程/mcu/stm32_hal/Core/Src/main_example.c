/*
 * ============================================================================
 *  main_example.c — STM32 HAL 主循环集成示例（F103C8T6，F4 用法相同）
 * ============================================================================
 *  本文件不是完整工程，而是演示如何把 motor_driver.h/.c 集成进
 *  CubeMX 生成的 main.c。把下面的关键片段合并进你的 main.c 即可：
 *
 *  【CubeMX 需要配置的内容】
 *    1. USARTx（例程用 USART2，对应引脚 PA2=TX、PA3=RX）：115200-8N1，
 *       使能 NVIC（USART2 global interrupt），其余默认。
 *    2. 时钟：HSE 8MHz → PLL → SYSCLK 72MHz（F1 蓝板常见配置）。
 *    3. 建议使能一个 1ms 时基（HAL_GetTick 默认 SysTick 1ms，无需额外配置）。
 *
 *  【需要加入工程的源文件】
 *    - Core/Inc/motor_driver.h   （加入 Include Paths）
 *    - Core/Src/motor_driver.c
 *    - 本文件中的函数/回调合并到 main.c（或单独建 motor_driver_app.c）
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
 *  例程行为：
 *    - 上电后每 50ms 发送一帧 0x31（ch1=200RPM、ch2=-200RPM、ch3/ch4=0）
 *    - 订阅状态上报（SUBSCRIBE 100ms），收到 0xF0 解析并打印前两路
 *    - USART2 接收中断逐字节喂给 md_rx_byte() 做滑动窗口帧解析
 * ============================================================================
 */
#include "main.h"                /* CubeMX 生成 */
#include "motor_driver.h"
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart2;

static uint8_t s_rx_byte;        /* 单字节接收缓冲 */

/* ============================================================================
 * 依赖注入回调：协议封装层通过此函数发送数据到 RC 口（USART2）。
 * ==========================================================================*/
void motor_driver_send_uart(uint8_t *buf, uint16_t len)
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
 * 0xF0 STATUS_REPORT 解析回调（覆盖 motor_driver.c 中的弱实现，规范 §4）
 *   常规模式 56B：[enc1~4:4×int32 LE][tgt1~4:4×float LE][rpm1~4:4×int32 LE]
 *                [sbus_frame_cnt:4B LE][sbus_ok_cnt:4B LE]
 *   说明：STM32 为小端，memcpy 到对应类型即可；下面给出 int32/float 的
 *         手动小端解析示例（float 位模式直接拷贝）。
 * ==========================================================================*/
static int32_t get_i32_le(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] |
                     ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) |
                     ((uint32_t)p[3] << 24));
}

static float get_f32_le(const uint8_t *p)
{
    float f;
    uint8_t tmp[4];

    tmp[0] = p[0]; tmp[1] = p[1]; tmp[2] = p[2]; tmp[3] = p[3];
    memcpy(&f, tmp, 4);          /* 小端机直接拷贝位模式 */
    return f;
}

void md_on_status_report(const uint8_t *data, uint8_t len)
{
    /* 常规模式 56B；扩展模式（DEBUG_SPEED=1）72B */
    if (len >= 56) {
        int32_t enc[4], rpm[4];
        float tgt[4];
        int i;

        for (i = 0; i < 4; i++) {
            enc[i] = get_i32_le(data + i * 4);                    /* 0..15  */
            tgt[i] = get_f32_le(data + 16 + i * 4);               /* 16..31 */
            rpm[i] = get_i32_le(data + 32 + i * 4);               /* 32..47 */
        }
        printf("0xF0 enc=%ld,%ld,%ld,%ld tgt=%.1f,%.1f,%.1f,%.1f rpm=%ld,%ld,%ld,%ld\r\n",
               (long)enc[0], (long)enc[1], (long)enc[2], (long)enc[3],
               (double)tgt[0], (double)tgt[1], (double)tgt[2], (double)tgt[3],
               (long)rpm[0], (long)rpm[1], (long)rpm[2], (long)rpm[3]);
    }
}

/* ============================================================================
 * 0x10 READ_PARAM 应答解析骨架（覆盖弱实现，规范 §5，config_t = 231B）
 *   演示读取 3 个字段：baud_rate(offset 11, u32)、cmd_timeout_ms(offset 15, u16)、
 *   control_mode(offset 18, u8)。全量解析请按规范 §5 的偏移表扩展。
 * ==========================================================================*/
void md_on_read_param(const uint8_t *cfg, uint16_t len)
{
    if (len >= 231) {
        uint32_t baud = (uint32_t)cfg[11] | ((uint32_t)cfg[12] << 8) |
                        ((uint32_t)cfg[13] << 16) | ((uint32_t)cfg[14] << 24);
        uint16_t timeout = (uint16_t)(cfg[15] | ((uint16_t)cfg[16] << 8));

        printf("READ_PARAM: baud=%lu timeout=%u mode=0x%02X\r\n",
               (unsigned long)baud, (unsigned)timeout, (unsigned)cfg[18]);
    }
}

/* ============================================================================
 * 主函数示例：合并进 CubeMX 生成的 main()。
 * ==========================================================================*/
int main(void)
{
    /* ---- CubeMX 生成的初始化（保持原样） ---- */
    HAL_Init();
    SystemClock_Config();          /* CubeMX 生成：72MHz */
    MX_GPIO_Init();
    MX_USART2_UART_Init();         /* 115200-8N1，NVIC 已使能 */

    /* ---- 应用初始化 ---- */
    /* 订阅状态上报：每 100ms 推送一帧 0xF0（需先于接收解析，控制板确认后回 ACK） */
    md_subscribe(100);
    /* 可选：请求全部配置（应答 231B 由 md_on_read_param 解析） */
    /* md_read_param(); */

    /* 启动 USART2 单字节接收中断（配合下方 HAL_UART_RxCpltCallback） */
    HAL_UART_Receive_IT(&huart2, &s_rx_byte, 1);

    /* ---- 主循环：每 50ms 发送一帧 0x31 控制帧 ---- */
    uint32_t last_send = HAL_GetTick();
    const int32_t targets[4] = { 200, -200, 0, 0 };   /* ch1=200RPM, ch2=-200RPM */

    while (1)
    {
        if (HAL_GetTick() - last_send >= 50) {
            md_motor_ctrl(targets);                    /* 连续发送，满足规范 §3.3 */
            last_send = HAL_GetTick();
        }
        /* 其它业务逻辑放这里 */
    }
}

/* ============================================================================
 * USART2 接收中断回调：每收到 1 字节喂给滑动窗口帧解析器。
 * （CubeMX 生成的 stm32f1xx_it.c 中已调用 HAL_UART_IRQHandler，
 *   本回调由 HAL 在中断上下文调用。）
 * ==========================================================================*/
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        md_rx_byte(s_rx_byte);                 /* 帧解析（含 0xF0 分发） */
        HAL_UART_Receive_IT(&huart2, &s_rx_byte, 1);   /* 重新挂接接收 */
    }
}
