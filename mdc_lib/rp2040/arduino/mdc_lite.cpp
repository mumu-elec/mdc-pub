/**
 * @file    mdc_lite.cpp
 * @brief   Motor Driver Controller 极简调用库 —— 只管调用（send-only）实现
 *
 * 规范依据：../LITE.md（mdc_lite 极简 LITE API）
 * 复用之 mdc_lib（API.md 的打包原语），本文件只是极薄封装：函数名收窄、
 * 关注范围收窄，协议字节布局（LE / CRC8 0x07 初值 0 / [AA][CMD][LEN][DATA][CRC]）不变。
 *
 * 本文件不 include 任何 Arduino / ESP 头文件（纯 C++，C++11 兼容）。
 *
 * 四平台（esp32 / rp2040 / avr / esp8266）此文件内容完全一致。
 */

#include "mdc_lite.h"

/* ==================== 发送侧（send-only） ==================== */

uint16_t md_lite_ctrl(int32_t m0, int32_t m1, int32_t m2, int32_t m3,
                      uint8_t* out, uint16_t cap)
{
    /* 委托 mdc_lib：0x31 四通道控制帧（含 SYNC+CRC8 整帧）
     * 校验向量：ctrl(100,-200,0,300) DATA == 64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00 */
    return md_bin_motor_ctrl(m0, m1, m2, m3, out, cap);
}

uint16_t md_lite_stop(uint8_t* out, uint16_t cap)
{
    /* 便捷：四通道全零控制帧（急停/退出前发送，DATA=16B 全零） */
    return md_lite_ctrl(0, 0, 0, 0, out, cap);
}

uint16_t md_lite_subscribe(uint16_t interval_ms, uint8_t* out, uint16_t cap)
{
    /* 委托 mdc_lib：0x40 订阅状态上报。
     * 校验向量：subscribe(50) 帧 == AA 40 02 32 00 <crc> */
    return md_bin_subscribe(interval_ms, out, cap);
}

uint16_t md_lite_unsubscribe(uint8_t* out, uint16_t cap)
{
    /* 委托 mdc_lib：0x41 取消订阅。
     * 校验向量：unsubscribe() 帧 == AA 41 00 <crc> */
    return md_bin_unsubscribe(out, cap);
}
