/**
 * @file    mdc_lite.h
 * @brief   Motor Driver Controller 极简调用库 —— 只管调用（send-only，独立实现）
 *
 * 定位：只服务于一个核心场景 —— 上位机调参、下位机执行。把「要发送的控制帧」
 *       打包好交给你发送，不解析任何回包（若需回读转速，请用 mdc_lite_ctrl.h）。
 *
 * **独立实现**：本库自带 CRC8、组帧与命令打包，**不依赖完整库 mdc_lib**，
 * 不 include "mdc_lib.h"，也不调用 md_bin_motor_* 等任何外部原语；只用到
 * 标准 C 头文件 <stdint.h>。协议字节布局（LE）与 CRC8（多项式 0x07、初值 0）
 * 与 mdc_lib / 协议规范完全一致，保证帧字节兼容。
 *
 * 只涉及 3 条命令：
 *   0x31 MOTOR_CTRL    四通道控制目标（下位机执行）
 *   0x40 SUBSCRIBE     开启状态周期上报（收速度的前提）
 *   0x41 UNSUBSCRIBE   关闭状态上报（善后）
 *
 * 帧格式：[0xAA][CMD][LEN][DATA...][CRC8]，CRC 范围 = CMD+LEN+DATA（不含 SYNC）。
 *
 * 纯 C，不 include 任何 HAL / pico-sdk / ESP-IDF 头文件，不碰串口外设
 * （串口收发由用户实现）。
 *
 * 用法（串口由你实现）：
 *   uint8_t  buf[32];
 *   uint16_t n;
 *   n = md_lite_subscribe(50, buf, sizeof(buf));     // AA 40 02 32 00 9E
 *   user_send(buf, n);
 *   n = md_lite_ctrl(100, -200, 0, 300, buf, sizeof(buf));  // 0x31 帧
 *   user_send(buf, n);
 *   n = md_lite_stop(buf, sizeof(buf));              // DATA 16B 全零
 *   user_send(buf, n);
 *   n = md_lite_unsubscribe(buf, sizeof(buf));       // AA 41 00 4E
 *   user_send(buf, n);
 */

#ifndef MDC_LITE_H
#define MDC_LITE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 常量（帧格式，独立定义，不依赖 mdc_lib.h） ==================== */

#define MD_LITE_SYNC           0xAAu   /* 二进制帧同步字 */
#define MD_LITE_MAX_DATA       248u    /* DATA 段最大长度（= config_t 大小） */
#define MD_LITE_CRC8_POLY      0x07u   /* CRC8 多项式（初值 0，按位计算） */

/* 仅关注的命令号（协议规范 §3.3），与 mdc_lib 字节一致 */
#define MD_LITE_CMD_MOTOR_CTRL   0x31u  /* 四通道批量控制（核心控制帧） */
#define MD_LITE_CMD_SUBSCRIBE    0x40u  /* 开启状态周期上报 */
#define MD_LITE_CMD_UNSUBSCRIBE  0x41u  /* 关闭状态上报 */
#define MD_LITE_CMD_STATUS_REPORT 0xF0u /* 状态上报（MCU 主动推送，引发速回调） */

/* 0xF0 STATUS_REPORT 的数据段长度（56B 常规 / 72B 扩展），自动兼容 */
#define MD_LITE_STATUS_LEN_56    56u
#define MD_LITE_STATUS_LEN_72    72u

/* ==================== 发送侧（send-only） ==================== */

/* CRC8-ATM：多项式 0x07，初值 0，按位计算。
 * 校验向量：md_lite_crc8({0x01,0x00})==0x15；md_lite_crc8("123456789")==0xF4。 */
uint8_t md_lite_crc8(const uint8_t* data, uint16_t len);

/* 0x31 四通道控制帧。目标值含义随各通道控制模式：
 *   open=PWM(±1000)、speed=RPM、pos=0.1°。
 * 验证向量：md_lite_ctrl(100,-200,0,300) 的 DATA 段
 *   == 64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00。
 * 返回写入字节数；cap 不足或参数非法返回 0。 */
uint16_t md_lite_ctrl(int32_t m0, int32_t m1, int32_t m2, int32_t m3,
                      uint8_t* out, uint16_t cap);

/* 便捷：ctrl(0,0,0,0) 全零控制帧（急停/退出前发送，DATA 16B 全零）。 */
uint16_t md_lite_stop(uint8_t* out, uint16_t cap);

/* 0x40 开启状态上报。interval_ms 建议 ≥20（固件钳位）。
 * 验证向量：md_lite_subscribe(50) 帧 = AA 40 02 32 00 9E。 */
uint16_t md_lite_subscribe(uint16_t interval_ms, uint8_t* out, uint16_t cap);

/* 0x41 关闭状态上报。验证向量：帧 = AA 41 00 4E。 */
uint16_t md_lite_unsubscribe(uint8_t* out, uint16_t cap);

#ifdef __cplusplus
}
#endif

#endif /* MDC_LITE_H */
