/**
 * @file    mdc_lite.c
 * @brief   Motor Driver Controller minimal call library - send-only
 *          (8051 / Keil C51, self-contained)
 *
 * LITE spec: ../../LITE.md
 *
 * This file includes NO hardware library headers and NO mdc_lib. It
 * implements its own CRC8 and frame assembly, so the frame layout / CRC8 /
 * little-endian order is identical to mdc_lib but nothing is shared with it.
 *
 * Keil C51 notes:
 *   - C89 style: declarations at block start, no // comments, no VLA,
 *     English comments only.
 *   - `out` is the caller's TX buffer; keep it in xdata on 8051 when large.
 *
 * GCC syntax check (xdata is not used in this file, so no bridge needed):
 *     gcc -std=c89 -Wall -Wextra -pedantic -fsyntax-only mdc_lite.c
 */

#include "mdc_lite.h"
#include <string.h>

/* ---- internal constants (MD_LITE_* keeps the namespace free of mdc_lib) ---- */
#define MD_LITE_SYNC         0xAAu   /* frame sync byte */
#define MD_LITE_MAX_DATA     248u    /* max DATA segment length (= config_t size) */
#define MD_LITE_CRC8_POLY    0x07u   /* CRC8 polynomial (init 0, bitwise) */
#define MD_LITE_CMD_CTRL     0x31u   /* 4-channel batch control */
#define MD_LITE_CMD_SUB      0x40u   /* subscribe status report */
#define MD_LITE_CMD_UNSUB    0x41u   /* unsubscribe status report */

/* ---- internal helpers: manual little-endian assembly ---- */
static void md_lite_put_u16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void md_lite_put_u32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/* ---- CRC8-ATM: poly 0x07, init 0, bitwise.
 * Check vectors: crc8({0x01,0x00})==0x15; crc8("123456789")==0xF4 ---- */
uint8_t md_lite_crc8(const uint8_t* data, uint16_t len)
{
    uint8_t c = 0;
    uint8_t b;
    uint16_t i;

    if (data == 0)
        return 0;

    for (i = 0; i < len; i++) {
        c ^= data[i];
        for (b = 0; b < 8; b++) {
            if (c & 0x80)
                c = (uint8_t)((c << 1) ^ MD_LITE_CRC8_POLY);
            else
                c = (uint8_t)(c << 1);
        }
    }
    return c;
}

/* ---- frame builder: [0xAA][CMD][LEN][DATA...][CRC8].
 * CRC covers CMD+LEN+DATA (SYNC excluded). Returns bytes written; 0 if
 * data_len > MD_LITE_MAX_DATA or cap too small. */
static uint16_t md_lite_build(uint8_t cmd, const uint8_t* data, uint16_t data_len,
                              uint8_t* out, uint16_t cap)
{
    uint16_t total;

    if (out == 0 || data_len > MD_LITE_MAX_DATA)
        return 0;
    total = (uint16_t)(data_len + 4);   /* SYNC + CMD + LEN + DATA + CRC */
    if (cap < total)
        return 0;

    out[0] = MD_LITE_SYNC;
    out[1] = cmd;
    out[2] = (uint8_t)data_len;
    if (data_len > 0) {
        if (data == 0)
            return 0;
        memcpy(out + 3, data, (size_t)data_len);
    }
    out[total - 1] = md_lite_crc8(out + 1, (uint16_t)(data_len + 2));
    return total;
}

/* ---- public send-only API ---- */

uint16_t md_lite_ctrl(int32_t m0, int32_t m1, int32_t m2, int32_t m3,
                      uint8_t* out, uint16_t cap)
{
    uint8_t data[16];

    md_lite_put_u32(data + 0,  (uint32_t)m0);
    md_lite_put_u32(data + 4,  (uint32_t)m1);
    md_lite_put_u32(data + 8,  (uint32_t)m2);
    md_lite_put_u32(data + 12, (uint32_t)m3);
    return md_lite_build(MD_LITE_CMD_CTRL, data, 16, out, cap);
}

uint16_t md_lite_stop(uint8_t* out, uint16_t cap)
{
    return md_lite_ctrl(0, 0, 0, 0, out, cap);
}

uint16_t md_lite_subscribe(uint16_t interval_ms, uint8_t* out, uint16_t cap)
{
    uint8_t data[2];

    md_lite_put_u16(data, interval_ms);
    return md_lite_build(MD_LITE_CMD_SUB, data, 2, out, cap);
}

uint16_t md_lite_unsubscribe(uint8_t* out, uint16_t cap)
{
    return md_lite_build(MD_LITE_CMD_UNSUB, NULL, 0, out, cap);
}
