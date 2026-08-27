/**
 * @file    mdc_lite_ctrl.h
 * @brief   Motor Driver Controller 极简调用库 —— 调用+回调接收（control + speed callback，独立实现）
 *
 * 规范依据：../LITE.md（mdc_lite 极简 LITE API，唯一依据）
 * 依赖：同目录 mdc_lite.h（send-only，独立实现，仅提供发送侧）。本文件**不依赖**
 *       mdc_lib.h（不调用 md_parser_* / md_parse_status），自带**滑窗流式解析器**。
 *
 * 在 send-only 全部函数基础上，增加一个"流式状态接收器"：逐字节喂入，自动找
 * 0xAA 同步、校验 CRC，收到 0xF0 STATUS_REPORT（56B/72B 自动兼容）时提取四通道
 * rpm 并调用用户注册的速度回调。
 *
 * 回调签名：typedef void (*md_lite_on_speed_t)(const int32_t rpm[4]);
 * 仅 0xF0 触发回调；其他帧（含文本回显/ACK/坏帧噪声）一律忽略，以便混流。
 *
 * 不碰串口：发送帧由 md_lite_* 打包、你自行写出；收到的字节逐字节喂 md_lite_ctrl_feed。
 * 纯 C++，不 include 任何 Arduino / ESP 头文件（本机 g++ -std=c++17 -Wall -Wextra 可直接编译校验）。
 *
 * 用法示例（.ino 中，串口由用户实现）：
 *   #include "mdc_lite_ctrl.h"
 *   static md_lite_ctrl_t g_ctrl;
 *   void on_speed(const int32_t rpm[4]) { Serial.printf("rpm=%ld,%ld,%ld,%ld\r\n",
 *       (long)rpm[0],(long)rpm[1],(long)rpm[2],(long)rpm[3]); }
 *
 *   void setup() {
 *       md_lite_ctrl_init(&g_ctrl, on_speed);
 *       uint8_t f[16];
 *       uint16_t n = md_lite_subscribe(50, f, sizeof(f)); // 与 send-only 同名同语义
 *       Serial2.write(f, n);
 *   }
 *   void loop() {
 *       while (Serial2.available()) {
 *           uint8_t b = (uint8_t)Serial2.read();
 *           md_lite_ctrl_feed(&g_ctrl, b);   // 0xF0 到达时自动回调 on_speed
 *       }
 *   }
 */

#ifndef MDC_LITE_CTRL_H
#define MDC_LITE_CTRL_H

#include <stdint.h>

#include "mdc_lite.h"

/* 流式解析器缓冲大小（可裁剪；默认 256 可容纳最大帧 235B（250 字节 DATA 亦兼容）。
 * 本库仅回读 0xF0（56B/72B，最大整帧 76B），小内存平台可设小（如 64/80），
 * 但需 ≥ 最大可见帧长以免溢出。 */
#ifndef MDC_LITE_PARSER_BUF
#define MDC_LITE_PARSER_BUF 256u
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 速度回调：0xF0 STATUS_REPORT 到达时调用，rpm 为四通道 int32 实时转速（rpm[0..3]）。
 * 仅 0xF0 触发；rpm 数组在本次回调内有效，请勿跨回调保存指针（如需保留请复制） */
typedef void (*md_lite_on_speed_t)(const int32_t rpm[4]);

/* 极简控制器状态（send-only + 回调接收）。
 * 内含自带的滑窗流式解析器缓冲（默认 256B，见 MDC_LITE_PARSER_BUF），
 * 请声明为全局/static，避免占栈 */
typedef struct {
    uint8_t            buf[MDC_LITE_PARSER_BUF];  /* 流式解析缓冲（自动找 0xAA 同步 + CRC 校验） */
    uint16_t           len;                       /* 缓冲内已累计字节数 */
    md_lite_on_speed_t on_speed;                  /* 用户速度回调（可为 NULL 则不派发） */
} md_lite_ctrl_t;

/* 注册速度回调并初始化流式解析器。cb 可为 NULL（不派发）。
 * 等价 mdc_lite 的初始化；调用一次后即可用 md_lite_ctrl_feed 逐字节喂 */
void md_lite_ctrl_init(md_lite_ctrl_t* self, md_lite_on_speed_t cb);

/* 喂一个收到的字节（0~255）。收到完整且 CRC 通过的 0xF0 STATUS_REPORT（56B/72B
 * 自动兼容）时解析四通道 rpm 并调用注册的速度回调；其他帧/噪声忽略。
 * 发送侧函数与 mdc_lite（send-only）完全同名同语义，见 mdc_lite.h */
void md_lite_ctrl_feed(md_lite_ctrl_t* self, uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif /* MDC_LITE_CTRL_H */
