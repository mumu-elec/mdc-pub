/**
 * @file    mdc_lite.c
 * @brief   Motor Driver Controller 极简调用库 —— 只管调用（send-only）纯 C 实现
 *
 * 薄封装：直接委托 mdc_lib 的 md_bin_motor_ctrl / md_bin_subscribe /
 * md_bin_unsubscribe。协议字节布局（LE）、CRC8 与帧格式全部由 mdc_lib 保证，
 * 本文件不重复实现任何协议逻辑，也不 include 任何硬件头文件。
 *
 * 代码风格：与 mdc_lib.c 一致（C89 兼容：声明在块首、无 // 注释、无变长数组），
 *           可被任意 C99/C11 工具链编译。
 */

#include "mdc_lite.h"

uint16_t md_lite_ctrl(int32_t m0, int32_t m1, int32_t m2, int32_t m3,
                      uint8_t* out, uint16_t cap)
{
    return md_bin_motor_ctrl(m0, m1, m2, m3, out, cap);
}

uint16_t md_lite_stop(uint8_t* out, uint16_t cap)
{
    return md_bin_motor_ctrl(0, 0, 0, 0, out, cap);
}

uint16_t md_lite_subscribe(uint16_t interval_ms, uint8_t* out, uint16_t cap)
{
    return md_bin_subscribe(interval_ms, out, cap);
}

uint16_t md_lite_unsubscribe(uint8_t* out, uint16_t cap)
{
    return md_bin_unsubscribe(out, cap);
}
