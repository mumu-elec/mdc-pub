/**
 * @file    mdc_lite.h
 * @brief   Motor Driver Controller minimal call library - send-only
 *          (LITE API, 8051 / Keil C51 port)
 *
 * LITE spec: ../../LITE.md (minimal API layered on top of mdc_lib)
 * API spec:  ../../API.md (unified md_lite_* signature set)
 *
 * Positioning: this library ONLY packs the frames you want to send. It never
 * parses a reply and never touches the UART; feed the returned bytes to your
 * own TX routine (e.g. the 8051 on-chip UART).
 *
 * Scope (send side of the 4 LITE commands):
 *   0x31 MOTOR_CTRL    4-channel control target (the device executes it)
 *   0x40 SUBSCRIBE     start periodic status report (prerequisite for speed)
 *   0x41 UNSUBSCRIBE   stop status report
 *   (the receive side - 0xF0 STATUS_REPORT / speed callback - is NOT handled
 *    here; use the sibling mdc_lite_ctrl for that.)
 *
 * Every function writes a complete binary frame
 *   [0xAA][CMD][LEN][DATA...][CRC8]
 * to `out` (little-endian, CRC8 poly 0x07 init 0, identical to mdc_lib) and
 * returns the number of bytes written, or 0 if `cap` is too small or the
 * arguments are invalid.
 *
 * Keil C51 notes:
 *   - C89 style: declarations at block start, no // comments, no VLA,
 *     English comments only (encoding-safe for Keil).
 *   - No static buffers in this file: `out` is the caller's TX buffer.
 *     On 8051 declare it in xdata when it is large, e.g.
 *         xdata uint8_t g_tx[20];
 * - This is a thin wrapper over mdc_lib.md_bin_motor_ctrl /
 *   md_bin_subscribe / md_bin_unsubscribe, so the byte layout is exactly the
 *   same as mdc_lib (nothing is re-implemented here).
 */

#ifndef MDC_LITE_H
#define MDC_LITE_H

#include <stdint.h>     /* Keil C51 provides <stdint.h> */

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Send-only API (LITE.md 2.1) ==================== */

/* 0x31 MOTOR_CTRL: four channel control targets (int32 little-endian).
 * Target meaning depends on the channel control mode:
 *   open = PWM (+-1000), speed = RPM, pos = 0.1 deg (+-3600 = +-360.0 deg).
 * Check vector: md_lite_ctrl(100,-200,0,300,..) DATA == 64 00 00 00 38 FF FF FF
 *   00 00 00 00 2C 01 00 00. Returns bytes written (20); 0 if cap < 20. */
uint16_t md_lite_ctrl(int32_t m0, int32_t m1, int32_t m2, int32_t m3,
                      uint8_t* out, uint16_t cap);

/* 0x31 convenience: four channel all-zero control frame (emergency stop /
 * exit). Exactly md_lite_ctrl(0,0,0,0,..); returns bytes written (20).
 * Check vector: AA 31 10 00*16 <crc>. */
uint16_t md_lite_stop(uint8_t* out, uint16_t cap);

/* 0x40 SUBSCRIBE: start periodic status report.
 * DATA = [interval_ms:2B LE]. Firmware clamps to >= 20 ms (the library does
 * not enforce that). Returns bytes written (6); 0 if cap < 6. */
uint16_t md_lite_subscribe(uint16_t interval_ms, uint8_t* out, uint16_t cap);

/* 0x41 UNSUBSCRIBE: stop status report. Returns bytes written (4). */
uint16_t md_lite_unsubscribe(uint8_t* out, uint16_t cap);

#ifdef __cplusplus
}
#endif

#endif /* MDC_LITE_H */
