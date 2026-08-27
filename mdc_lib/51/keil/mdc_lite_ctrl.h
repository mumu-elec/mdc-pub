/**
 * @file    mdc_lite_ctrl.h
 * @brief   Motor Driver Controller minimal call library - control + speed
 *          callback (LITE API, 8051 / Keil C51 port)
 *
 * LITE spec: ../../LITE.md
 *
 * On top of the send-only mdc_lite functions this adds a streaming STATUS
 * receiver: feed received bytes in one at a time; when a complete CRC-valid
 * 0xF0 STATUS_REPORT (56B / 72B, auto-detected) arrives the four channel rpm
 * values are parsed (md_parse_status) and dispatched to the registered speed
 * callback. Any other frame (ACK, text echo, noise before 0xAA) is ignored,
 * so the parser can share one UART stream with other traffic.
 *
 * Reuse: md_parser_t / md_parser_feed / md_parse_status / MD_CMD_STATUS_REPORT
 * from the sibling mdc_lib.h, and the send-only md_lite_* functions from
 * mdc_lite.h. Nothing here re-implements protocol bytes.
 *
 * Keil C51 notes:
 *   - C89 style, English comments only.
 *   - The md_parser_t instance is owned by the caller (declare it in xdata,
 *     e.g.  xdata md_parser_t g_parser;  because it holds a MD_PARSER_BUF-byte
 *     buffer). The callback and the intermediate md_status_t are module-static
 *     and live in this file's xdata to spare 8051 internal RAM.
 *   - Only ONE receiver instance is supported (one UART link), which is the
 *     normal 8051 case.
 */

#ifndef MDC_LITE_CTRL_H
#define MDC_LITE_CTRL_H

#include <stdint.h>
#include "mdc_lite.h"     /* send-only md_lite_* functions */
#include "mdc_lib.h"      /* md_parser_t / md_parse_status / MD_CMD_* */

#ifdef __cplusplus
extern "C" {
#endif

/* Speed callback: receives the four channel RPM values (int32). The array
 * points into the module's status buffer and is only valid for the duration
 * of the call; copy the values if you must keep them. */
typedef void (*md_lite_on_speed_t)(const int32_t rpm[4]);

/* Register the speed callback and initialise the streaming parser.
 * `parser` is the caller's md_parser_t (xdata on 8051). `on_speed` may be 0
 * to disable dispatch. Call once at start-up before feeding bytes. */
void md_lite_ctrl_init(md_parser_t* parser, md_lite_on_speed_t on_speed);

/* Feed one received byte (0~255). When a complete CRC-valid 0xF0 STATUS_REPORT
 * frame is reconstructed, the four channel rpm are parsed and the speed
 * callback is invoked; every other frame / noise byte is ignored. Call this
 * from your UART RX interrupt (or whenever a byte arrives). */
void md_lite_ctrl_feed(md_parser_t* parser, uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif /* MDC_LITE_CTRL_H */
