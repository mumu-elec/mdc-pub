/**
 * @file    mdc_lite.h
 * @brief   Motor Driver Controller 极简调用库 —— 只管调用（send-only，独立实现）
 *
 * 规范依据：../LITE.md（mdc_lite 极简 LITE API，唯一依据）
 *
 * **独立实现**：本文件（及 mdc_lite.cpp）自带 CRC8、组帧与命令打包，
 * **不依赖**本目录 mdc_lib.h / mdc_lib.cpp（不 include "mdc_lib.h"，
 * 不调用 md_bin_motor_* / md_parser_* / md_parse_status）。协议字节布局（LE、
 * CRC8-ATM 多项式 0x07 初值 0、[0xAA][CMD][LEN][DATA][CRC]）与 mdc_lib / API.md
 * 完全一致（各平台字节级兼容，已与 Python 独立实现比对）。
 *
 * 定位（上位机调参、下位机执行）：只关心"发什么"，不解析任何回包。
 * 只涉及 3 条命令的发送侧（0x31 / 0x40 / 0x41）；如需回读转速，请用同目录
 * mdc_lite_ctrl（调用+回调接收）。
 *
 * 不碰串口：本文件只把"要发送的控制帧"写入 out 缓冲并返回字节数；
 * 串口收发由用户实现（拿返回的字节自己 SerialX.write(buf, n) 发送）。
 * 纯 C++，不 include 任何 Arduino / ESP 头文件（本机 g++ -std=c++17 -Wall -Wextra
 * 可直接编译校验）。
 *
 * 用法示例（.ino 中，串口由用户实现）：
 *   #include "mdc_lite.h"
 *   uint8_t buf[32];
 *   uint16_t n = md_lite_ctrl(100, -200, 0, 300, buf, sizeof(buf));
 *   Serial2.write(buf, n);                 // 0x31 四通道控制帧（20B）
 *
 * 校验向量（与 mdc_lib / API.md / Python 独立实现完全一致）：
 *   md_lite_ctrl(100,-200,0,300)  帧 == AA 31 10 64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00 E7
 *   md_lite_subscribe(50)         帧 == AA 40 02 32 00 9E
 *   md_lite_unsubscribe()         帧 == AA 41 00 4E
 *   md_lite_stop()                帧 == AA 31 10 00...00 3F   （DATA 16B 全零）
 */

#ifndef MDC_LITE_H
#define MDC_LITE_H

#include <stdint.h>

/* ==================== 极简协议常量（与 mdc_lib / API.md / 协议规范一致） ==================== */

#define MD_SYNC              0xAAu   /* 二进制帧同步字 */
#define MD_MAX_DATA          248u    /* DATA 段最大长度 */
#define MD_CRC8_POLY         0x07u   /* CRC8 多项式（初值 0，按位计算，CRC8-ATM） */

#define MD_CMD_MOTOR_CTRL    0x31u   /* 四通道批量控制（核心控制帧） */
#define MD_CMD_SUBSCRIBE     0x40u   /* 开启状态周期上报 */
#define MD_CMD_UNSUBSCRIBE   0x41u   /* 关闭状态上报 */
#define MD_CMD_STATUS_REPORT 0xF0u   /* 状态上报（MCU 主动推送，供 mdc_lite_ctrl 回读） */

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 底层：CRC8 ==================== */

/* CRC8-ATM：多项式 0x07，初值 0，按位计算。
 * 校验向量：md_lite_crc8({0x01,0x00},2)==0x15；md_lite_crc8("123456789",9)==0xF4 */
uint8_t md_lite_crc8(const uint8_t* data, uint16_t len);

/* ==================== 发送侧（send-only，只打包要发送的控制帧） ==================== */

/* 0x31 MOTOR_CTRL：四通道控制目标（int32 LE）。
 * 目标值含义随当前通道控制模式：open=PWM(±1000)  speed=RPM  pos=0.1°(±3600=±360.0°)。
 * 成功返回写入字节数（20），cap 不足或 out 为空返回 0。
 * 校验向量：ctrl(100,-200,0,300) 帧 == AA 31 10 64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00 E7 */
uint16_t md_lite_ctrl(int32_t m0, int32_t m1, int32_t m2, int32_t m3,
                      uint8_t* out, uint16_t cap);

/* 0x31 MOTOR_CTRL 便捷：四通道全零控制帧（急停/退出前发送，DATA=16B 全零） */
uint16_t md_lite_stop(uint8_t* out, uint16_t cap);

/* 0x40 SUBSCRIBE：开启状态周期上报。
 * interval_ms 建议 ≥20（固件钳位）；成功返回字节数（6），cap 不足返回 0。
 * 校验向量：subscribe(50) 帧 == AA 40 02 32 00 9E */
uint16_t md_lite_subscribe(uint16_t interval_ms, uint8_t* out, uint16_t cap);

/* 0x41 UNSUBSCRIBE：关闭状态上报。成功返回字节数（4），cap 不足返回 0。
 * 校验向量：unsubscribe() 帧 == AA 41 00 4E */
uint16_t md_lite_unsubscribe(uint8_t* out, uint16_t cap);

#ifdef __cplusplus
}
#endif

#endif /* MDC_LITE_H */
