/**
 * @file    mdc_lite.h
 * @brief   Motor Driver Controller 极简调用库 —— 只管调用（send-only）
 *
 * 定位：在 mdc_lib（通用调用库）之上的**极简封装**，只服务于一个核心场景：
 *       上位机调参、下位机执行 —— 把"要发送的控制帧"打包好交给你发送。
 *       不解析任何回包（若需回读转速，请用 mdc_lite_ctrl.h）。
 *
 * 只涉及 3 条命令：
 *   0x31 MOTOR_CTRL    四通道控制目标（下位机执行）
 *   0x40 SUBSCRIBE     开启状态周期上报（收速度的前提）
 *   0x41 UNSUBSCRIBE   关闭状态上报（善后）
 *
 * 字节布局（LE）、CRC8（多项式 0x07、初值 0）、帧格式
 *   [0xAA][CMD][LEN][DATA...][CRC8]  与 mdc_lib / 协议规范完全一致。
 * 各函数直接委托 mdc_lib 的 md_bin_motor_ctrl / md_bin_subscribe /
 * md_bin_unsubscribe，不重复实现协议，不改变任何字节。
 *
 * 纯 C，不 include 任何 HAL 头文件，不碰串口外设（串口收发由用户实现）。
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
 *
 * 依赖：同目录 mdc_lib.h（本头文件 #include 它，仅做薄封装）。
 */

#ifndef MDC_LITE_H
#define MDC_LITE_H

#include "mdc_lib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 发送侧（send-only） ==================== */

/* 0x31 四通道控制帧。目标值含义随各通道控制模式：
 *   open=PWM(±1000)、speed=RPM、pos=0.1°。
 * 验证向量：md_lite_ctrl(100,-200,0,300) 的 DATA 段
 *   == 64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00。
 * 返回写入字节数；cap 不足或参数非法返回 0（直接委托 md_bin_motor_ctrl）。 */
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
