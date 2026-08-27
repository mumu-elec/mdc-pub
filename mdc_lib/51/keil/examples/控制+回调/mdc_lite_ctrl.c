/**
 * @file    mdc_lite_ctrl.c
 * @brief   Motor Driver Controller minimal call library - control + speed
 *          callback (8051 / Keil C51, self-contained)
 *
 * LITE spec: ../../LITE.md
 *
 * This file adds the streaming 0xF0 STATUS_REPORT receiver on top of the
 * send-only mdc_lite functions. It implements its own sliding-window 0xAA+CRC8
 * parser, so the frame layout / CRC / little-endian order match mdc_lib exactly
 * but nothing is shared with it.
 *
 * Keil C51 notes:
 *   - C89 style: declarations at block start, no // comments, no VLA,
 *     English comments only.
 *   - The parser buffer and state are a module-static MD_51_XDATA buffer, so
 *     they occupy 8051 external RAM (xdata) instead of the tiny internal RAM.
 *   - Only one receiver instance is supported (one UART link).
 *
 * GCC syntax check (MD_51_XDATA expands to the real xdata keyword on Keil/SDCC,
 * nothing on host gcc, so the file compiles here unchanged):
 *     gcc -std=c89 -Wall -Wextra -pedantic -fsyntax-only mdc_lite_ctrl.c
 */

#include "mdc_lite_ctrl.h"
#include <string.h>

/* ---- internal constants (MD_LITE_* keeps the namespace free of mdc_lib) ---- */
#define MD_LITE_CTRL_MAX_DATA 250u   /* max DATA segment length */
#define MD_LITE_CTRL_STATUS   0xF0u  /* STATUS_REPORT: the only frame we act on */

/* Sliding window is fixed: the largest 0xF0 STATUS_REPORT frame is 72B DATA
 * -> whole frame 76B. 80B fits it with a little slack for noise tolerance. */
#define MD_LITE_CTRL_BUF      80u

/* xdata is a Keil C51 keyword, not a macro. This bridge lets the file be
 * syntax-checked with a plain host gcc: on 8051 toolchains (Keil __C51__ /
 * __CX51__, SDCC __SDCC__) MD_51_XDATA expands to the real xdata keyword,
 * elsewhere to nothing. Same approach as the sibling full library. */
#ifndef MD_51_XDATA
#if defined(__C51__) || defined(__CX51__) || defined(__SDCC__)
#define MD_51_XDATA xdata
#else
#define MD_51_XDATA
#endif
#endif

/* ---- single-receiver parser state (one UART link) ---- */
static MD_51_XDATA uint8_t s_buf[MD_LITE_CTRL_BUF];
static uint16_t s_len;
static md_lite_on_speed_t s_on_speed;

/* Drop the first byte of the buffer (a false sync) and rescan the rest for the
 * next 0xAA (sliding window). */
static void md_lite_ctrl_drop_first(void)
{
    uint16_t i;

    if (s_len <= 1) {
        s_len = 0;
        return;
    }
    for (i = 1; i < s_len; i++) {
        if (s_buf[i] == 0xAAu) {
            memmove(s_buf, s_buf + i, (size_t)(s_len - i));
            s_len = (uint16_t)(s_len - i);
            return;
        }
    }
    s_len = 0;
}

static int32_t md_lite_ctrl_get_i32(const uint8_t* p)
{
    return (int32_t)((uint32_t)p[0]
                   | ((uint32_t)p[1] << 8)
                   | ((uint32_t)p[2] << 16)
                   | ((uint32_t)p[3] << 24));
}

/* Try to complete one frame from the buffer. Returns 1 and fills cmd/payload/
 * payload_len when a CRC-valid frame is delivered (payload points into s_buf;
 * s_len is reset so the payload is valid until the next feed). Returns 0 when
 * more bytes are needed. False syncs are dropped and the window rescanned
 * internally, so several noise bytes may be consumed in one call. */
static int md_lite_ctrl_step(uint8_t* cmd, const uint8_t** payload, uint16_t* plen)
{
    uint8_t ln;
    uint16_t need;
    int done;

    done = 0;
    while (!done) {
        if (s_len < 3)
            return 0;
        ln = s_buf[2];
        if (ln > MD_LITE_CTRL_MAX_DATA) {       /* illegal LEN: false sync */
            md_lite_ctrl_drop_first();
            continue;
        }
        need = (uint16_t)(ln + 4);              /* SYNC+CMD+LEN+DATA+CRC */
        if (s_len < need)
            return 0;
        if (md_lite_crc8(s_buf + 1, (uint16_t)(ln + 2)) != s_buf[need - 1]) {
            md_lite_ctrl_drop_first();          /* CRC failed: keep sliding */
            continue;
        }
        if (cmd != 0) *cmd = s_buf[1];
        if (payload != 0) *payload = s_buf + 3;
        if (plen != 0) *plen = ln;
        s_len = 0;
        done = 1;
    }
    return 1;
}

void md_lite_ctrl_init(md_lite_on_speed_t on_speed)
{
    s_on_speed = on_speed;
    s_len = 0;
}

void md_lite_ctrl_feed(uint8_t byte)
{
    uint8_t cmd;
    const uint8_t* payload;
    uint16_t plen;

    /* Idle: only a sync byte 0xAA starts a frame (text echo is noise here) */
    if (s_len == 0) {
        if (byte == 0xAAu) {
            s_buf[0] = byte;
            s_len = 1;
        }
        return;
    }

    /* Buffer full (noise piled up): slide the window forward */
    if (s_len >= MD_LITE_CTRL_BUF)
        md_lite_ctrl_drop_first();

    s_buf[s_len] = byte;
    s_len++;

    if (md_lite_ctrl_step(&cmd, &payload, &plen)) {
        /* Only 0xF0 STATUS_REPORT triggers the speed callback. Everything else
         * (ACK, text echo, other frames) is ignored, so the parser can run on a
         * mixed stream with other traffic. */
        if (cmd == MD_LITE_CTRL_STATUS && (plen == 56 || plen == 72)) {
            int32_t rpm[4];

            rpm[0] = md_lite_ctrl_get_i32(payload + 32);
            rpm[1] = md_lite_ctrl_get_i32(payload + 36);
            rpm[2] = md_lite_ctrl_get_i32(payload + 40);
            rpm[3] = md_lite_ctrl_get_i32(payload + 44);
            if (s_on_speed != 0)
                s_on_speed(rpm);
        }
    }
}
