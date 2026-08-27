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
 * Rebuild / run (mdc_lite is self-contained, so mdc_lib.c is NOT needed):
 *     gcc -std=c89 -Wall -Wextra -pedantic \
 *         -o ttest mdc_lite.c mdc_lite_ctrl.c test_mdc_lite.c
 *     ./ttest          (expect "ALL OK"; non-zero exit on failure)
 *
 * The 8051 `xdata` keyword is bridged to nothing on host gcc (MD_51_XDATA),
 * so these files compile here unchanged.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

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

/* ---- test-local frame builder: [0xAA][CMD][LEN][DATA...][CRC8].
 * Uses the library's exposed md_lite_crc8 so the parser and this builder agree
 * on the CRC. Used only to synthesize 0xF0 STATUS_REPORT frames to feed. */
static uint16_t build_frame(uint8_t cmd, const uint8_t* data,
                            uint16_t len, uint8_t* out, uint16_t cap)
{
    uint16_t total = (uint16_t)(len + 4);

    if (cap < total)
        return 0;
    out[0] = 0xAA;
    out[1] = cmd;
    out[2] = (uint8_t)len;
    if (len > 0)
        memcpy(out + 3, data, (size_t)len);
    out[total - 1] = md_lite_crc8(out + 1, (uint16_t)(len + 2));
    return total;
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
        check(md_lite_crc8(v1, 2) == 0x15, "crc8({0x01,0x00}) == 0x15");
        check(md_lite_crc8((const uint8_t*)"123456789", 9) == 0xF4,
              "crc8(\"123456789\") == 0xF4");
    }

    /* ctrl data + framing */
    n = md_lite_ctrl(100, -200, 0, 300, frame, sizeof(frame));
    check(n == 20, "md_lite_ctrl returns 20 bytes");
    check(frame[1] == 0x31 && frame[2] == 16, "ctrl CMD=0x31 LEN=16");
    check(memcmp(frame + 3, ctrl_data, 16) == 0,
          "ctrl DATA == 64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00");
    check(md_lite_crc8(frame + 1, 18) == frame[19], "ctrl frame CRC correct");

    /* subscribe(50): AA 40 02 32 00 <crc> */
    n = md_lite_subscribe(50, frame, sizeof(frame));
    check(n == 6, "md_lite_subscribe returns 6 bytes");
    check(frame[0] == 0xAA && frame[1] == 0x40 && frame[2] == 2
          && frame[3] == 0x32 && frame[4] == 0x00,
          "subscribe(50) == AA 40 02 32 00 <crc>");
    check(md_lite_crc8(frame + 1, 4) == frame[5], "subscribe frame CRC correct");

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

    /* stop is exactly ctrl(0,0,0,0) */
    {
        uint8_t a[20], b[20];
        md_lite_ctrl(0, 0, 0, 0, a, sizeof(a));
        md_lite_stop(b, sizeof(b));
        check(memcmp(a, b, 20) == 0, "md_lite_stop == md_lite_ctrl(0,0,0,0)");
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

        n = build_frame(0xF0, payload, 56, frame, sizeof(frame));
    }
    check(n == 60, "built 56B 0xF0 frame (60B)");

    /* init receiver with the callback */
    md_lite_ctrl_init(on_speed);

    /* feed a non-0xF0 (0x31) frame first: must NOT trigger callback */
    g_count = 0;
    n = md_lite_ctrl(100, 0, 0, 0, frame, sizeof(frame));
    for (i = 0; i < n; i++)
        md_lite_ctrl_feed(frame[i]);
    check(g_count == 0, "non-0xF0 frame does not trigger callback");

    /* feed a 56B 0xF0 frame byte-by-byte */
    {
        uint8_t payload[56];
        const int32_t rpm[4] = { 1234, -567, 0, 9000 };
        const int32_t enc[4] = { 100, 200, 300, 400 };
        const float   tgt[4] = { 10.0f, 20.0f, 0.0f, 5.0f };
        int j;

        for (j = 0; j < 4; j++) {
            put_i32(payload + 0 + j * 4, enc[j]);
            put_f32(payload + 16 + j * 4, tgt[j]);
            put_i32(payload + 32 + j * 4, rpm[j]);
        }
        put_u32(payload + 48, 7);
        put_u32(payload + 52, 6);
        n = build_frame(0xF0, payload, 56, frame, sizeof(frame));
    }
    g_count = 0;
    for (i = 0; i < n; i++)
        md_lite_ctrl_feed(frame[i]);
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

        n = build_frame(0xF0, payload, 72, frame, sizeof(frame));
    }
    check(n == 76, "built 72B 0xF0 frame (76B)");

    g_count = 0;
    for (i = 0; i < n; i++)
        md_lite_ctrl_feed(frame[i]);
    check(g_count == 1 && g_rpm[0] == 10 && g_rpm[1] == 20
          && g_rpm[2] == 30 && g_rpm[3] == 40,
          "72B 0xF0 frame parses rpm correctly");

    /* interleaved noise bytes before a frame must be tolerated */
    g_count = 0;
    {
        static const uint8_t junk2[8] = { 0x00, 0xFF, 0xAA, 0xAA, 0x00, 0x11, 0x22, 0x33 };
        for (i = 0; i < sizeof(junk2); i++)
            md_lite_ctrl_feed(junk2[i]);
    }
    check(g_count == 0, "random noise does not trigger callback");

    /* noise followed by a valid 56B frame is still decoded */
    {
        uint8_t payload[56];
        const int32_t rpm[4] = { 5, 6, 7, 8 };
        int j;

        for (j = 0; j < 4; j++) {
            put_i32(payload + 0 + j * 4, 0);
            put_f32(payload + 16 + j * 4, 0.0f);
            put_i32(payload + 32 + j * 4, rpm[j]);
        }
        put_u32(payload + 48, 0);
        put_u32(payload + 52, 0);
        n = build_frame(0xF0, payload, 56, frame, sizeof(frame));
    }
    g_count = 0;
    {
        static const uint8_t jn[5] = { 0x11, 0x22, 0x33, 0x44, 0x55 };
        for (i = 0; i < sizeof(jn); i++)
            md_lite_ctrl_feed(jn[i]);
    }
    for (i = 0; i < n; i++)
        md_lite_ctrl_feed(frame[i]);
    check(g_count == 1 && g_rpm[0] == 5 && g_rpm[1] == 6
          && g_rpm[2] == 7 && g_rpm[3] == 8,
          "noise before 0xF0 frame is tolerated, rpm decoded");

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
