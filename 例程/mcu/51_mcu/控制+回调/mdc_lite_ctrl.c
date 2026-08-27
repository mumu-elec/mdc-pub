/**
 * @file    mdc_lite_ctrl.c
 * @brief   Motor Driver Controller minimal call library - control + speed
 *          callback (8051 / Keil C51)
 *
 * LITE spec: ../../LITE.md
 *
 * This file adds the streaming 0xF0 STATUS_REPORT receiver on top of the
 * send-only mdc_lite functions. It reuses md_parser_feed / md_parse_status
 * from the sibling mdc_lib, so frame layout / CRC8 / little-endian match
 * mdc_lib exactly.
 *
 * Keil C51 notes:
 *   - C89 style: declarations at block start, no // comments, no VLA,
 *     English comments only.
 *   - The intermediate md_status_t is a module-static xdata buffer (~73B) so
 *     it does not consume 8051 internal RAM.
 *   - Only one receiver instance is supported (one UART link).
 *
 * GCC syntax check (xdata bridged to nothing on host gcc):
 *     gcc -std=c89 -fsyntax-only mdc_lite_ctrl.c
 */

#include "mdc_lite_ctrl.h"

/* xdata is a Keil C51 keyword, not a macro. This bridge lets the file be
 * syntax-checked with a plain host gcc: on 8051 toolchains (Keil __C51__ /
 * __CX51__, SDCC __SDCC__) MD_51_XDATA expands to the real xdata keyword,
 * elsewhere to nothing. Same approach as mdc_lib.c. */
#ifndef MD_51_XDATA
#if defined(__C51__) || defined(__CX51__) || defined(__SDCC__)
#define MD_51_XDATA xdata
#else
#define MD_51_XDATA
#endif
#endif

/* Single-receiver state (one UART link): the last parsed STATUS_REPORT and
 * the speed callback. md_status_t is ~73B, so it lives in xdata on 8051. */
static MD_51_XDATA md_status_t s_status;
static md_lite_on_speed_t s_on_speed;

void md_lite_ctrl_init(md_parser_t* parser, md_lite_on_speed_t on_speed)
{
    s_on_speed = on_speed;
    if (parser != 0)
        md_parser_init(parser);
}

void md_lite_ctrl_feed(md_parser_t* parser, uint8_t byte)
{
    uint8_t cmd;
    const uint8_t* payload;
    uint16_t plen;

    if (parser == 0)
        return;

    if (!md_parser_feed(parser, byte, &cmd, &payload, &plen))
        return;

    /* Only 0xF0 STATUS_REPORT triggers the speed callback. Everything else
     * (ACK, text echo, noise before 0xAA) is ignored, so the parser can run
     * on a mixed stream with other traffic. */
    if (cmd == MD_CMD_STATUS_REPORT) {
        if (md_parse_status(payload, plen, &s_status)) {
            if (s_on_speed != 0)
                s_on_speed(s_status.rpm);
        }
    }
}
