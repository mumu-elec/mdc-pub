/**
 * @file    mdc_lite.cpp
 * @brief   Motor Driver Controller 极简调用库 —— 只管调用（send-only，独立实现）
 *
 * 规范依据：../LITE.md（mdc_lite 极简 LITE API）
 *
 * **独立实现**：本文件自带 CRC8 与全部组帧逻辑，不复用本目录 mdc_lib.cpp
 * （不调用 md_bin_motor_ctrl / md_bin_subscribe / md_bin_unsubscribe）。
 * 协议字节布局（LE）、CRC8-ATM（0x07 初值 0）与帧格式
 * [0xAA][CMD][LEN][DATA...][CRC8] 与 mdc_lib / API.md 完全一致，仅实现
 * "极简 4 条命令"的发送侧。写入 out 缓冲并返回字节数；cap 不足返回 0。
 *
 * 本文件不 include 任何 Arduino / ESP 头文件（纯 C++，C++11 兼容）。
 *
 * 四平台（esp32 / rp2040 / avr / esp8266）此文件内容完全一致。
 */

#include "mdc_lite.h"

/* ==================== 内部工具：小端手动拼装（16 位 int 平台如 AVR 上行为一致） ==================== */

static inline void md_lite_put_u32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/* ==================== 底层：CRC8 ==================== */

uint8_t md_lite_crc8(const uint8_t* data, uint16_t len)
{
    uint8_t c = 0;

    if (data == nullptr)
        return 0;

    for (uint16_t i = 0; i < len; i++) {
        c ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (c & 0x80)
                c = (uint8_t)((c << 1) ^ MD_CRC8_POLY);
            else
                c = (uint8_t)(c << 1);
        }
    }
    return c;
}

/* ==================== 内部工具：组帧 ==================== */

/* 组帧：[0xAA][CMD][LEN][DATA...][CRC8]，CRC 范围 = CMD+LEN+DATA（不含 SYNC）。
 * 返回写入字节数；data_len>MD_MAX_DATA 或 cap 不足/参数非法返回 0。 */
static inline uint16_t md_lite_build_frame(uint8_t cmd, const uint8_t* data,
                                           uint16_t data_len, uint8_t* out, uint16_t cap)
{
    uint16_t total;

    if (out == nullptr || data_len > (uint16_t)MD_MAX_DATA)
        return 0;
    total = (uint16_t)(data_len + 4);   /* SYNC + CMD + LEN + DATA + CRC */
    if (cap < total)
        return 0;

    out[0] = MD_SYNC;
    out[1] = cmd;
    out[2] = (uint8_t)data_len;
    if (data_len > 0) {
        if (data == nullptr)
            return 0;
        for (uint16_t i = 0; i < data_len; i++)
            out[3 + i] = data[i];
    }
    out[total - 1] = md_lite_crc8(out + 1, (uint16_t)(data_len + 2));   /* CMD+LEN+DATA */
    return total;
}

/* ==================== 发送侧（send-only） ==================== */

uint16_t md_lite_ctrl(int32_t m0, int32_t m1, int32_t m2, int32_t m3,
                      uint8_t* out, uint16_t cap)
{
    uint8_t data[16];
    int32_t t[4];

    if (out == nullptr || cap < 20)
        return 0;

    t[0] = m0; t[1] = m1; t[2] = m2; t[3] = m3;
    for (uint16_t i = 0; i < 4; i++)
        md_lite_put_u32(data + i * 4, (uint32_t)t[i]);

    /* 0x31 四通道控制帧。校验向量：ctrl(100,-200,0,300)
     * DATA == 64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00（整帧 AA 31 10 ... E7） */
    return md_lite_build_frame(MD_CMD_MOTOR_CTRL, data, (uint16_t)sizeof(data), out, cap);
}

uint16_t md_lite_stop(uint8_t* out, uint16_t cap)
{
    /* 便捷：四通道全零控制帧（急停/退出前发送，DATA=16B 全零） */
    return md_lite_ctrl(0, 0, 0, 0, out, cap);
}

uint16_t md_lite_subscribe(uint16_t interval_ms, uint8_t* out, uint16_t cap)
{
    uint8_t data[2];

    if (out == nullptr || cap < 6)
        return 0;

    data[0] = (uint8_t)(interval_ms & 0xFF);
    data[1] = (uint8_t)((interval_ms >> 8) & 0xFF);

    /* 0x40 订阅状态上报。校验向量：subscribe(50) 帧 == AA 40 02 32 00 9E */
    return md_lite_build_frame(MD_CMD_SUBSCRIBE, data, (uint16_t)sizeof(data), out, cap);
}

uint16_t md_lite_unsubscribe(uint8_t* out, uint16_t cap)
{
    /* 0x41 取消订阅。校验向量：unsubscribe() 帧 == AA 41 00 4E */
    return md_lite_build_frame(MD_CMD_UNSUBSCRIBE, nullptr, 0, out, cap);
}
