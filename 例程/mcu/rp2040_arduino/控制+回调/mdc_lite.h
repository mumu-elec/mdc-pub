/**
 * @file    mdc_lite.h
 * @brief   Motor Driver Controller 极简调用库 —— 只管调用（send-only）
 *
 * 规范依据：../LITE.md（mdc_lite 极简 LITE API，唯一依据）
 * 复用：本目录 mdc_lib.h / mdc_lib.cpp（API.md 的打包原语，协议字节布局不变）。
 *
 * 定位（上位机调参、下位机执行）：只关心"发什么"，不解析任何回包。
 * 只涉及 2 类命令的发送侧：
 *   0x31 MOTOR_CTRL    四通道控制目标（下位机执行）
 *   0x40 SUBSCRIBE     开启状态周期上报（收速度的前提）
 *   0x41 UNSUBSCRIBE   关闭状态上报（善后）
 * 如需回读转速，请用同目录 mdc_lite_ctrl（调用+回调接收）。
 *
 * 不碰串口：本文件只把"要发送的控制帧"写入 out 缓冲并返回字节数；
 * 串口收发由用户实现（拿返回的字节自己 SerialX.write(buf, n) 发送）。
 * 纯 C++，不 include 任何 Arduino / ESP 头文件（本机 g++ -std=c++17 -Wall -Wextra 可直接编译校验）。
 *
 * 用法示例（.ino 中，串口由用户实现）：
 *   #include "mdc_lite.h"
 *   uint8_t buf[32];
 *   uint16_t n = md_lite_ctrl(100, -200, 0, 300, buf, sizeof(buf));
 *   Serial2.write(buf, n);                 // 0x31 四通道控制帧（20B）
 *
 * 校验向量（与 mdc_lib / API.md 完全一致）：
 *   md_lite_ctrl(100,-200,0,300)  的 DATA == 64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00
 *   md_lite_subscribe(50) 帧 == AA 40 02 32 00 <crc>
 *   md_lite_unsubscribe() 帧 == AA 41 00 <crc>
 *   md_lite_stop()        帧 == AA 31 10 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 <crc>
 */

#ifndef MDC_LITE_H
#define MDC_LITE_H

#include <stdint.h>

/* 复用 mdc_lib 的底层原语（md_bin_motor_ctrl / md_bin_subscribe / md_bin_unsubscribe） */
#include "mdc_lib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 发送侧（send-only，只打包要发送的控制帧） ==================== */

/* 0x31 MOTOR_CTRL：四通道控制目标（int32 LE）。
 * 目标值含义随当前通道控制模式：open=PWM(±1000)  speed=RPM  pos=0.1°(±3600=±360.0°)。
 * 委托 md_bin_motor_ctrl；成功返回写入字节数（20），cap 不足返回 0 */
uint16_t md_lite_ctrl(int32_t m0, int32_t m1, int32_t m2, int32_t m3,
                      uint8_t* out, uint16_t cap);

/* 0x31 MOTOR_CTRL 便捷：四通道全零控制帧（急停/退出前发送，DATA=16B 全零） */
uint16_t md_lite_stop(uint8_t* out, uint16_t cap);

/* 0x40 SUBSCRIBE：开启状态周期上报。
 * interval_ms 建议 ≥20（固件钳位）；委托 md_bin_subscribe；返回字节数（6），cap 不足返回 0 */
uint16_t md_lite_subscribe(uint16_t interval_ms, uint8_t* out, uint16_t cap);

/* 0x41 UNSUBSCRIBE：关闭状态上报。委托 md_bin_unsubscribe；返回字节数（4），cap 不足返回 0 */
uint16_t md_lite_unsubscribe(uint8_t* out, uint16_t cap);

#ifdef __cplusplus
}
#endif

#endif /* MDC_LITE_H */
