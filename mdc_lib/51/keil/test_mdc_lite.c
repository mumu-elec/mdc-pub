/**
 * @file    test_mdc_lite.c
 * @brief   HOST-ONLY self-check for mdc_lite / mdc_lite_ctrl (8051/Keil C51 port)
 *
 * !!! DO NOT add this file to a Keil project !!!
 * This is NOT 8051 target code. It builds and runs on a desktop C compiler
 * (host gcc) to validate the C library against the LITE.md consistency
 * vectors. It uses stdio/printf and a file-local main(), which do not belong
 * on an MCU target.
 *
 * Rebuild / run:
 *     gcc -std=c89 -DMD_ENABLE_CONFIG=0 -Wall -Wextra \
 *         -o ttest mdc_lib.c mdc_lite.c mdc_lite_ctrl.c test_mdc_lite.c
 *     ./ttest          (expect "ALL OK"; non-zero exit on failure)
 *
 * The 8051 `xdata` keyword is bridged to nothing on host gcc (MD_51_XDATA),
 * so these files compile here unchanged.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "mdc_lib.h"
#include "mdc_lite.h"
#include "mdc_lite_ctrl.h"

static int g_fails = 0;

static void check(int cond, const char* msg)
{
    if (cond) {
        printf("ok   - %s\n", msg);
    } else {
        printf("FAIL - %s\n", msg);
        g_fails++;
    }
}

/* ---- little-endian byte helpers for building test payloads ---- */
static void put_u32(uint8_t* p, uint32_t u)
{
    p[0] = (uint8_t)(u & 0xFF);
    p[1] = (uint8_t)((u >> 8) & 0xFF);
    p[2] = (uint8_t)((u >> 16) & 0xFF);
    p[3] = (uint8_t)((u >> 24) & 0xFF);
}

static void put_i32(uint8_t* p, int32_t v)
{
    put_u32(p, (uint32_t)v);
}

static void put_f32(uint8_t* p, float f)
{
    uint32_t u;

    memcpy(&u, &f, 4);
    put_u32(p, u);
}

/* ---- speed callback capture ---- */
static int32_t g_rpm[4];
static int32_t g_count;

static void on_speed(const int32_t rpm[4])
{
    g_count++;
    g_rpm[0] = rpm[0];
    g_rpm[1] = rpm[1];
    g_rpm[2] = rpm[2];
    g_rpm[3] = rpm[3];
}

/* ---- 1. send-only byte vectors (LITE.md 2.2 / API.md 8) ---- */
static void test_send_only(void)
{
    uint8_t frame[32];
    uint16_t n;
    static const uint8_t ctrl_data[16] = {
        0x64, 0x00, 0x00, 0x00,   /* 100     */
        0x38, 0xFF, 0xFF, 0xFF,   /* -200    */
        0x00, 0x00, 0x00, 0x00,   /* 0       */
        0x2C, 0x01, 0x00, 0x00    /* 300     */
    };

    /* CRC8 check vectors */
    {
        static const uint8_t v1[2] = { 0x01, 0x00 };
        check(md_crc8(v1, 2) == 0x15, "crc8({0x01,0x00}) == 0x15");
        check(md_crc8((const uint8_t*)"123456789", 9) == 0xF4,
              "crc8(\"123456789\") == 0xF4");
    }

    /* ctrl data + framing */
    n = md_lite_ctrl(100, -200, 0, 300, frame, sizeof(frame));
    check(n == 20, "md_lite_ctrl returns 20 bytes");
    check(frame[1] == 0x31 && frame[2] == 16, "ctrl CMD=0x31 LEN=16");
    check(memcmp(frame + 3, ctrl_data, 16) == 0,
          "ctrl DATA == 64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00");
    check(md_crc8(frame + 1, 18) == frame[19], "ctrl frame CRC correct");

    /* subscribe(50): AA 40 02 32 00 <crc> */
    n = md_lite_subscribe(50, frame, sizeof(frame));
    check(n == 6, "md_lite_subscribe returns 6 bytes");
    check(frame[0] == 0xAA && frame[1] == 0x40 && frame[2] == 2
          && frame[3] == 0x32 && frame[4] == 0x00,
          "subscribe(50) == AA 40 02 32 00 <crc>");
    check(md_crc8(frame + 1, 4) == frame[5], "subscribe frame CRC correct");

    /* unsubscribe: AA 41 00 <crc> */
    n = md_lite_unsubscribe(frame, sizeof(frame));
    check(n == 4 && frame[1] == 0x41 && frame[2] == 0x00,
          "unsubscribe == AA 41 00 <crc>");

    /* stop == ctrl(0,0,0,0): AA 31 10 + 16x 00 + crc */
    n = md_lite_stop(frame, sizeof(frame));
    check(n == 20 && frame[1] == 0x31 && frame[2] == 16,
          "stop CMD=0x31 LEN=16");
    check(memcmp(frame + 3, (const uint8_t*)"\x00\x00\x00\x00"   \
                       "\x00\x00\x00\x00\x00\x00\x00\x00"        \
                       "\x00\x00\x00\x00", 16) == 0,
          "stop DATA == 16B all-zero");

    /* equivalence with mdc_lib packer */
    {
        uint8_t a[32], b[32];
        md_lite_ctrl(1, 2, 3, 4, a, sizeof(a));
        md_bin_motor_ctrl(1, 2, 3, 4, b, sizeof(b));
        check(memcmp(a, b, 20) == 0, "md_lite_ctrl == md_bin_motor_ctrl");
    }

    /* cap-too-small returns 0 */
    check(md_lite_ctrl(1, 2, 3, 4, frame, 10) == 0,
          "ctrl returns 0 when cap too small");
}

/* ---- 2. callback receiver: 0xF0 flow feeding ---- */
static void test_ctrl_callback(void)
{
    uint8_t frame[96];
    uint16_t n;
    uint16_t i;
    md_parser_t parser;

    /* build a 56B STATUS_REPORT payload */
    {
        uint8_t payload[56];
        const int32_t rpm[4] = { 1234, -567, 0, 9000 };
        const int32_t enc[4] = { 100, 200, 300, 400 };
        const float   tgt[4] = { 10.0f, 20.0f, 0.0f, 5.0f };
        int j;

        for (j = 0; j < 4; j++) {
            put_i32(payload + 0 + j * 4, enc[j]);       /* enc  @0   */
            put_f32(payload + 16 + j * 4, tgt[j]);      /* tgt  @16  */
            put_i32(payload + 32 + j * 4, rpm[j]);      /* rpm  @32  */
        }
        put_u32(payload + 48, 7);                        /* sbus_frame_cnt @48 */
        put_u32(payload + 52, 6);                        /* sbus_ok_cnt   @52 */

        n = md_build_frame(MD_CMD_STATUS_REPORT, payload, 56, frame, sizeof(frame));
    }
    check(n == 60, "built 56B 0xF0 frame (60B)");

    /* init receiver with the callback */
    md_parser_init(&parser);
    md_lite_ctrl_init(&parser, on_speed);

    /* feed noise + a non-0xF0 (0x31) frame first: must NOT trigger callback */
    g_count = 0;
    {
        static const uint8_t junk[21] = {
            0xAA, 0x31, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11
        };
        for (i = 0; i < sizeof(junk); i++)
            md_lite_ctrl_feed(&parser, junk[i]);
    }
    check(g_count == 0, "non-0xF0 frame / noise does not trigger callback");

    /* feed a 56B 0xF0 frame byte-by-byte */
    for (i = 0; i < n; i++)
        md_lite_ctrl_feed(&parser, frame[i]);
    check(g_count == 1, "0xF0 frame triggers callback once");
    check(g_rpm[0] == 1234 && g_rpm[1] == -567 && g_rpm[2] == 0 && g_rpm[3] == 9000,
          "callback rpm == (1234, -567, 0, 9000)");

    /* build a 72B extended STATUS_REPORT payload */
    {
        uint8_t payload[72];
        const int32_t rpm72[4] = { 10, 20, 30, 40 };
        int j;

        for (j = 0; j < 4; j++) {
            put_i32(payload + 0 + j * 4, 0);             /* enc   @0  */
            put_f32(payload + 16 + j * 4, 0.0f);         /* tgt   @16 */
            put_i32(payload + 32 + j * 4, rpm72[j]);     /* rpm   @32 */
            put_i32(payload + 48 + j * 4, 0);            /* raw   @48 */
        }
        put_u32(payload + 64, 1);                        /* sbus_frame_cnt @64 */
        put_u32(payload + 68, 1);                        /* sbus_ok_cnt   @68 */

        n = md_build_frame(MD_CMD_STATUS_REPORT, payload, 72, frame, sizeof(frame));
    }
    check(n == 76, "built 72B 0xF0 frame (76B)");

    g_count = 0;
    for (i = 0; i < n; i++)
        md_lite_ctrl_feed(&parser, frame[i]);
    check(g_count == 1 && g_rpm[0] == 10 && g_rpm[1] == 20
          && g_rpm[2] == 30 && g_rpm[3] == 40,
          "72B 0xF0 frame parses rpm correctly");

    /* interleaved noise bytes before the frame must be tolerated */
    g_count = 0;
    {
        static const uint8_t junk2[8] = { 0x00, 0xFF, 0xAA, 0xAA, 0x00, 0x11, 0x22, 0x33 };
        for (i = 0; i < sizeof(junk2); i++)
            md_lite_ctrl_feed(&parser, junk2[i]);
    }
    check(g_count == 0, "random noise does not trigger callback");

    printf("\n");
}

int main(void)
{
    test_send_only();
    test_ctrl_callback();

    if (g_fails == 0) {
        printf("ALL OK\n");
        return 0;
    }
    printf("\n%d check(s) FAILED\n", g_fails);
    return 1;
}
