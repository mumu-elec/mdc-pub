/**
 * @file    mdc_lite.c
 * @brief   Motor Driver Controller 极简调用库 —— 只管调用（send-only）纯 C 实现
 *
 * **独立实现**：自带 CRC8、组帧与 0x31/0x40/0x41 命令打包，**不依赖完整库 mdc_lib**，
 * 不 include "mdc_lib.h"，不调用 md_bin_motor_ctrl / md_bin_subscribe /
 * md_bin_unsubscribe 等任何外部原语。CRC8 与帧字节布局与 mdc_lib 完全一致。
 *
 * 代码风格：C89 兼容（声明在块首、无 // 注释、无变长数组），
 *           可被任意 C99/C11 工具链编译。
 */

#include "mdc_lite.h"

/* 小端写 32 位（LE），与 mdc_lib 的 md_put_u32 字节序一致 */
static void md_lite_put_u32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/* CRC8-ATM：多项式 0x07，初值 0，按位计算 */
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

/* 通用组帧：[0xAA][CMD][LEN][DATA...][CRC8]，CRC 范围 = CMD+LEN+DATA（不含 SYNC）。
 * 返回写入字节数；data_len > MD_LITE_MAX_DATA 或 cap 不足返回 0。 */
static uint16_t md_lite_build_frame(uint8_t cmd, const uint8_t* data,
                                    uint16_t data_len, uint8_t* out, uint16_t cap)
{
    uint16_t total;
    uint16_t i;

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
        for (i = 0; i < data_len; i++)
            out[3 + i] = data[i];
    }
    out[total - 1] = md_lite_crc8(out + 1, (uint16_t)(data_len + 2));
    return total;
}

uint16_t md_lite_ctrl(int32_t m0, int32_t m1, int32_t m2, int32_t m3,
                      uint8_t* out, uint16_t cap)
{
    uint8_t data[16];

    md_lite_put_u32(data + 0,  (uint32_t)m0);
    md_lite_put_u32(data + 4,  (uint32_t)m1);
    md_lite_put_u32(data + 8,  (uint32_t)m2);
    md_lite_put_u32(data + 12, (uint32_t)m3);
    return md_lite_build_frame(MD_LITE_CMD_MOTOR_CTRL, data, 16, out, cap);
}

uint16_t md_lite_stop(uint8_t* out, uint16_t cap)
{
    return md_lite_ctrl(0, 0, 0, 0, out, cap);
}

uint16_t md_lite_subscribe(uint16_t interval_ms, uint8_t* out, uint16_t cap)
{
    uint8_t data[2];

    data[0] = (uint8_t)(interval_ms & 0xFF);
    data[1] = (uint8_t)((interval_ms >> 8) & 0xFF);
    return md_lite_build_frame(MD_LITE_CMD_SUBSCRIBE, data, 2, out, cap);
}

uint16_t md_lite_unsubscribe(uint8_t* out, uint16_t cap)
{
    return md_lite_build_frame(MD_LITE_CMD_UNSUBSCRIBE, NULL, 0, out, cap);
}
