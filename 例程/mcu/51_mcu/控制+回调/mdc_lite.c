/**
 * @file    mdc_lite.c
 * @brief   Motor Driver Controller minimal call library - send-only
 *          (8051 / Keil C51)
 *
 * LITE spec: ../../LITE.md
 *
 * This file includes NO hardware library headers and NO static buffers. It
 * only forwards to the sibling mdc_lib pack functions (mdc_lib.h / mdc_lib.c),
 * so the frame layout / CRC8 / little-endian assembly is IDENTICAL to mdc_lib.
 *
 * Keil C51 notes:
 *   - C89 style: declarations at block start, no // comments, no VLA,
 *     English comments only.
 *   - `out` is the caller's TX buffer; keep it in xdata on 8051 when large.
 *
 * GCC syntax check (xdata is bridged to nothing on host gcc, see mdc_lib.c):
 *     gcc -std=c89 -fsyntax-only mdc_lite.c
 */

#include "mdc_lite.h"
#include "mdc_lib.h"

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
