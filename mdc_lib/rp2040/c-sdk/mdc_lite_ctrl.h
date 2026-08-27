/**
 * @file    mdc_lite_ctrl.h
 * @brief   Motor Driver Controller 极简调用库 —— 调用 + 回调接收（control + speed callback）
 *
 * 在 mdc_lite（send-only）基础上增加一个**流式状态接收器**：你逐字节喂入收到的数据，
 * 遇到下位机主动推送的 0xF0 STATUS_REPORT（56B/72B 自动兼容）时自动解析四通道
 * rpm 并调用你注册的**速度回调**。
 *
 * **独立实现**：本头文件只 #include 同族独立极简库 "mdc_lite.h"（复用其 md_lite_crc8 与
 * 帧常量），**不引用完整库 mdc_lib.h**，不依赖 md_parser_t / md_parse_status 等外部原语；
 * 自带滑窗找 0xAA + CRC8 校验的流式解析器。
 *
 * 只涉及 4 条命令：0x31 MOTOR_CTRL、0x40/0x41 SUBSCRIBE/UNSUBSCRIBE、0xF0 STATUS_REPORT。
 *
 * 发送侧函数与 mdc_lite 完全同名同语义（本头文件 #include "mdc_lite.h"），本文件
 * 仅新增「回调接收器」。因此：只需发送可只引 mdc_lite.h；需回读转速引本文件即可
 * 同时获得发送与回调。
 *
 * 纯 C，不 include 任何 HAL / pico-sdk / ESP-IDF 头文件。
 *
 * 用法（串口收发由你实现，含回调）：
 *   static void on_speed(const int32_t rpm[4])   // 使用 rpm[0..3]
 *   {
 *   }
 *
 *   md_lite_ctrl_t c;
 *   md_lite_ctrl_init(&c, on_speed);
 *   uint8_t  buf[32];
 *   uint16_t n = md_lite_subscribe(50, buf, sizeof(buf));   // 收速度的前提
 *   user_send(buf, n);
 *   while (1) {
 *       n = md_lite_ctrl(100, 0, 0, 0, buf, sizeof(buf));   // 发送控制帧
 *       user_send(buf, n);
 *       // 收到一字节就喂给接收器：0xF0 到达时自动回调 on_speed
 *       md_lite_ctrl_feed(&c, rx_byte);
 *   }
 */

#ifndef MDC_LITE_CTRL_H
#define MDC_LITE_CTRL_H

#include "mdc_lite.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 接收滑窗缓冲大小（可裁剪；默认 256 可容纳最大帧 4+250=254B。
 * 0xF0 状态帧最大 4+72=76B，因此 128 已足够；小内存平台可再调小。 */
#ifndef MD_LITE_BUF_SIZE
#define MD_LITE_BUF_SIZE 256u
#endif

/* 速度回调：四通道实时转速 rpm[0..3]（int32）。仅 0xF0 完整且 CRC 通过的帧触发 */
typedef void (*md_lite_on_speed_t)(const int32_t rpm[4]);

/* 极简控制器状态：自带流式解析缓冲 + 速度回调。
 * 接收缓冲内嵌于结构体，**不依赖外部解析器**；小内存平台请声明在静态区/
 * 全局区（避免占栈），例如 51 上的 xdata。 */
typedef struct {
    uint8_t           buf[MD_LITE_BUF_SIZE];  /* 流式接收滑窗缓冲 */
    uint16_t          len;                    /* 当前缓冲有效字节数 */
    md_lite_on_speed_t on_speed;              /* 速度回调（可为 NULL，则只解析不派发） */
} md_lite_ctrl_t;

/* 注册速度回调并初始化流式解析器。cb 可为 NULL（只解析不派发）。
 * 内部重置接收缓冲。 */
void md_lite_ctrl_init(md_lite_ctrl_t* c, md_lite_on_speed_t cb);

/* 逐字节喂入。0xF0 完整帧（56B/72B）到达时解析 rpm 并调用注册的回调；
 * 其它帧（0x31 控制帧、ACK、文本回显等）按协议丢弃，不触发回调。
 * 无论是否触发回调，都会推进内部流式解析器状态机（滑窗找 0xAA + CRC 校验）。 */
void md_lite_ctrl_feed(md_lite_ctrl_t* c, uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif /* MDC_LITE_CTRL_H */
