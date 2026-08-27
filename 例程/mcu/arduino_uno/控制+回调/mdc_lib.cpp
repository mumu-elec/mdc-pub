/**
 * @file    mdc_lib.cpp
 * @brief   Motor Driver Controller 通用调用库 —— 纯 C++ 实现（Arduino 平台）
 *
 * 协议依据：../例程/common/协议规范.md（布局 v2.1，config_t = 231B）
 * API 规范：../API.md（统一 md_* 签名，C 风格输出缓冲）
 *
 * 本文件不 include 任何 Arduino / ESP 头文件（纯 C++，C++11 兼容），
 * 只做字节打包与解析；串口收发由用户实现。
 *
 * 四平台（esp32 / rp2040 / avr / esp8266）此文件内容完全一致。
 */

#include "mdc_lib.h"

#include <string.h>   /* memcpy / memmove */

/* ==================== 内部工具：多字节小端手动拼装 / 拆解 ====================
 * 说明：所有移位前先提升到目标无符号宽度，保证 16 位 int 平台（AVR）上
 *       行为一致；float 位模式通过 memcpy 搬运，避免类型别名与对齐问题。
 *       全部 static inline，置 MD_ENABLE_CONFIG=0 裁剪时也不会产生未使用告警。 */

static inline void md_put_u16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static inline void md_put_u32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static inline void md_put_f32(uint8_t* p, float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    md_put_u32(p, u);
}

static inline uint16_t md_get_u16(const uint8_t* p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t md_get_u32(const uint8_t* p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static inline int32_t md_get_i32(const uint8_t* p)
{
    return (int32_t)md_get_u32(p);
}

static inline float md_get_f32(const uint8_t* p)
{
    uint32_t u;
    float f;
    u = md_get_u32(p);
    memcpy(&f, &u, 4);
    return f;
}

/* ==================== 底层：CRC / 组帧 / 帧解析 ==================== */

uint8_t md_crc8(const uint8_t* data, uint16_t len)
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

uint16_t md_build_frame(uint8_t cmd, const uint8_t* data, uint16_t data_len,
                        uint8_t* out, uint16_t cap)
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
        memcpy(out + 3, data, (size_t)data_len);
    }
    out[total - 1] = md_crc8(out + 1, (uint16_t)(data_len + 2));   /* CMD+LEN+DATA */
    return total;
}

int md_parse_frame(const uint8_t* frame, uint16_t len,
                   uint8_t* cmd, const uint8_t** payload, uint16_t* payload_len)
{
    uint8_t ln;

    if (frame == nullptr || len < 4 || frame[0] != MD_SYNC)
        return 0;
    ln = frame[2];
    if (ln > (uint16_t)MD_MAX_DATA)
        return 0;
    if (len < (uint16_t)(4 + ln))
        return 0;
    if (md_crc8(frame + 1, (uint16_t)(ln + 2)) != frame[3 + ln])
        return 0;

    if (cmd != nullptr)         *cmd = frame[1];
    if (payload != nullptr)     *payload = frame + 3;
    if (payload_len != nullptr) *payload_len = ln;
    return 1;
}

/* ==================== 流式解析器 ==================== */

void md_parser_init(md_parser_t* p)
{
    if (p != nullptr)
        p->len = 0;
}

/* 丢弃当前缓冲首字节（视为假同步），并在剩余缓冲中重扫下一个 0xAA */
static inline void md_parser_drop_first(md_parser_t* p)
{
    uint16_t i;

    if (p->len <= 1) {
        p->len = 0;
        return;
    }
    for (i = 1; i < p->len; i++) {
        if (p->buf[i] == MD_SYNC) {
            memmove(p->buf, p->buf + i, (size_t)(p->len - i));
            p->len = (uint16_t)(p->len - i);
            return;
        }
    }
    p->len = 0;
}

int md_parser_feed(md_parser_t* p, uint8_t byte,
                   uint8_t* cmd, const uint8_t** payload, uint16_t* payload_len)
{
    uint8_t  ln;
    uint16_t need;

    if (p == nullptr)
        return 0;

    /* 空闲状态：只认同步字 0xAA（文本回显等噪声在此被丢弃） */
    if (p->len == 0) {
        if (byte == MD_SYNC) {
            p->buf[0] = byte;
            p->len = 1;
        }
        return 0;
    }

    /* 缓冲已满（MD_PARSER_BUF 过小或噪声堆积）：丢首字节腾位 */
    if (p->len >= (uint16_t)MD_PARSER_BUF)
        md_parser_drop_first(p);

    p->buf[p->len] = byte;
    p->len++;

    /* 至少需要 CMD + LEN 才能判断帧长 */
    if (p->len < 3)
        return 0;

    ln = p->buf[2];
    if (ln > (uint16_t)MD_MAX_DATA) {   /* LEN 非法：丢弃该帧起点，重扫 */
        md_parser_drop_first(p);
        return 0;
    }

    need = (uint16_t)(3 + ln + 1);      /* CMD + LEN + DATA + CRC */
    if (p->len < need)
        return 0;

    if (md_crc8(p->buf + 1, (uint16_t)(ln + 2)) == p->buf[need - 1]) {
        /* CRC 通过：输出一帧。payload 指向 p->buf 内部，下次 feed 前必须消费 */
        if (cmd != nullptr)         *cmd = p->buf[1];
        if (payload != nullptr)     *payload = p->buf + 3;
        if (payload_len != nullptr) *payload_len = ln;
        p->len = 0;
        return 1;
    }

    /* CRC 失败：丢弃该帧起点，继续滑动扫描 */
    md_parser_drop_first(p);
    return 0;
}

/* ==================== 文本指令层 ==================== */

/* uint8 -> 十进制字符串，返回字符数（不含 NUL） */
static inline uint16_t md_u8_to_dec(uint8_t v, char* out)
{
    char tmp[4];
    uint16_t i = 0;
    uint16_t j;
    uint16_t n;

    do {
        tmp[i] = (char)('0' + (v % 10));
        i++;
        v = (uint8_t)(v / 10);
    } while (v != 0);
    n = i;
    for (j = 0; j < n; j++)
        out[j] = tmp[n - 1 - j];
    out[n] = '\0';
    return n;
}

uint16_t md_text_build(const char* cmd, const char* args, char* out, uint16_t cap)
{
    uint16_t i = 0;
    char c;

    if (cmd == nullptr || out == nullptr)
        return 0;

    while ((c = *cmd++) != '\0') {
        if (i + 3 > cap)            /* 当前字符 + '\n' + NUL */
            return 0;
        out[i++] = c;
    }
    if (args != nullptr) {
        if (i + 3 > cap)
            return 0;
        out[i++] = ' ';
        while ((c = *args++) != '\0') {
            if (i + 3 > cap)
                return 0;
            out[i++] = c;
        }
    }
    if (i + 2 > cap)                /* '\n' + NUL */
        return 0;
    out[i++] = '\n';
    out[i] = '\0';
    return i;                       /* 不含 NUL 的字节数 */
}

uint16_t md_text_version(char* out, uint16_t cap)
{
    return md_text_build("/version", nullptr, out, cap);
}

uint16_t md_text_help(char* out, uint16_t cap)
{
    return md_text_build("/help", nullptr, out, cap);
}

uint16_t md_text_status(char* out, uint16_t cap)
{
    return md_text_build("/status", nullptr, out, cap);
}

uint16_t md_text_check(char* out, uint16_t cap)
{
    return md_text_build("/check", nullptr, out, cap);
}

uint16_t md_text_detect(char* out, uint16_t cap)
{
    return md_text_build("/detect", nullptr, out, cap);
}

uint16_t md_text_save(char* out, uint16_t cap)
{
    return md_text_build("/save", nullptr, out, cap);
}

uint16_t md_text_load(char* out, uint16_t cap)
{
    return md_text_build("/load", nullptr, out, cap);
}

uint16_t md_text_reset(char* out, uint16_t cap)
{
    return md_text_build("/reset", nullptr, out, cap);
}

uint16_t md_text_enczero(uint8_t ch, char* out, uint16_t cap)
{
    char num[4];

    if (ch < 1 || ch > 4)
        return 0;
    md_u8_to_dec(ch, num);
    return md_text_build("/enczero", num, out, cap);
}

uint16_t md_text_mode(uint8_t ch, const char* mode, char* out, uint16_t cap)
{
    char args[16];
    char num[4];
    uint16_t i = 0;
    uint16_t j;

    if (ch < 1 || ch > 4)
        return 0;
    md_u8_to_dec(ch, num);
    for (j = 0; num[j] != '\0'; j++)
        args[i++] = num[j];
    if (mode != nullptr) {
        args[i++] = ' ';
        for (j = 0; mode[j] != '\0'; j++) {
            if (i + 1 >= (uint16_t)sizeof(args))    /* args 溢出保护 */
                return 0;
            args[i++] = mode[j];
        }
    }
    args[i] = '\0';
    return md_text_build("/mode", args, out, cap);
}

/* ==================== 二进制命令层（15 个打包函数） ==================== */

uint16_t md_bin_ping(uint8_t* out, uint16_t cap)
{
    return md_build_frame(MD_CMD_PING, nullptr, 0, out, cap);
}

uint16_t md_bin_read_param(uint8_t* out, uint16_t cap)
{
    return md_build_frame(MD_CMD_READ_PARAM, nullptr, 0, out, cap);
}

#if MD_ENABLE_CONFIG
uint16_t md_bin_write_param(const md_config_t* cfg, uint8_t* out, uint16_t cap)
{
    uint8_t tmp[MD_CONFIG_SIZE];
    uint16_t n;

    if (cfg == nullptr || out == nullptr)
        return 0;
    n = md_pack_config(cfg, tmp, (uint16_t)sizeof(tmp));
    if (n == 0)
        return 0;
    return md_build_frame(MD_CMD_WRITE_PARAM, tmp, n, out, cap);
}
#endif /* MD_ENABLE_CONFIG */

uint16_t md_bin_write_field(uint16_t field_id, const uint8_t* value,
                            uint16_t value_len, uint8_t* out, uint16_t cap)
{
    uint16_t total;

    if (out == nullptr)
        return 0;
    if (value_len > (uint16_t)(MD_MAX_DATA - 2))    /* 2B field_id + value */
        return 0;
    total = (uint16_t)(value_len + 6);              /* 4 帧头尾 + 2 field_id + value */
    if (cap < total)
        return 0;

    out[0] = MD_SYNC;
    out[1] = MD_CMD_WRITE_FIELD;
    out[2] = (uint8_t)(value_len + 2);
    out[3] = (uint8_t)(field_id & 0xFF);
    out[4] = (uint8_t)((field_id >> 8) & 0xFF);
    if (value_len > 0) {
        if (value == nullptr)
            return 0;
        memcpy(out + 5, value, (size_t)value_len);
    }
    out[5 + value_len] = md_crc8(out + 1, (uint16_t)(value_len + 4));
    return total;
}

uint16_t md_bin_save(uint8_t* out, uint16_t cap)
{
    return md_build_frame(MD_CMD_SAVE_EEPROM, nullptr, 0, out, cap);
}

uint16_t md_bin_load(uint8_t* out, uint16_t cap)
{
    return md_build_frame(MD_CMD_LOAD_EEPROM, nullptr, 0, out, cap);
}

uint16_t md_bin_factory_reset(uint8_t* out, uint16_t cap)
{
    return md_build_frame(MD_CMD_FACTORY_RESET, nullptr, 0, out, cap);
}

uint16_t md_bin_motor_raw(uint8_t ch, uint8_t dir, uint16_t pwm,
                          uint8_t* out, uint16_t cap)
{
    if (out == nullptr || cap < 8)
        return 0;
    if (ch > 3 || dir > 1 || pwm > 1000)
        return 0;   /* 参数非法 */

    out[0] = MD_SYNC;
    out[1] = MD_CMD_MOTOR_RAW;
    out[2] = 4;
    out[3] = ch;
    out[4] = dir;
    out[5] = (uint8_t)(pwm & 0xFF);
    out[6] = (uint8_t)((pwm >> 8) & 0xFF);
    out[7] = md_crc8(out + 1, 6);
    return 8;
}

uint16_t md_bin_motor_ctrl(int32_t t0, int32_t t1, int32_t t2, int32_t t3,
                           uint8_t* out, uint16_t cap)
{
    if (out == nullptr || cap < 20)
        return 0;

    out[0] = MD_SYNC;
    out[1] = MD_CMD_MOTOR_CTRL;
    out[2] = 16;
    md_put_u32(out + 3,  (uint32_t)t0);
    md_put_u32(out + 7,  (uint32_t)t1);
    md_put_u32(out + 11, (uint32_t)t2);
    md_put_u32(out + 15, (uint32_t)t3);
    out[19] = md_crc8(out + 1, 18);
    return 20;
}

uint16_t md_bin_subscribe(uint16_t interval_ms, uint8_t* out, uint16_t cap)
{
    if (out == nullptr || cap < 6)
        return 0;

    out[0] = MD_SYNC;
    out[1] = MD_CMD_SUBSCRIBE;
    out[2] = 2;
    out[3] = (uint8_t)(interval_ms & 0xFF);
    out[4] = (uint8_t)((interval_ms >> 8) & 0xFF);
    out[5] = md_crc8(out + 1, 4);
    return 6;
}

uint16_t md_bin_unsubscribe(uint8_t* out, uint16_t cap)
{
    return md_build_frame(MD_CMD_UNSUBSCRIBE, nullptr, 0, out, cap);
}

uint16_t md_bin_debug_sbus(uint8_t enable, uint8_t* out, uint16_t cap)
{
    if (out == nullptr || cap < 5)
        return 0;

    out[0] = MD_SYNC;
    out[1] = MD_CMD_DEBUG_SBUS;
    out[2] = 1;
    out[3] = enable ? 1 : 0;
    out[4] = md_crc8(out + 1, 3);
    return 5;
}

uint16_t md_bin_debug_speed(uint8_t enable, uint8_t* out, uint16_t cap)
{
    if (out == nullptr || cap < 5)
        return 0;

    out[0] = MD_SYNC;
    out[1] = MD_CMD_DEBUG_SPEED;
    out[2] = 1;
    out[3] = enable ? 1 : 0;
    out[4] = md_crc8(out + 1, 3);
    return 5;
}

uint16_t md_bin_enter_bl(uint8_t* out, uint16_t cap)
{
    return md_build_frame(MD_CMD_ENTER_BL, nullptr, 0, out, cap);
}

uint16_t md_bin_reboot(uint8_t* out, uint16_t cap)
{
    return md_build_frame(MD_CMD_REBOOT, nullptr, 0, out, cap);
}

/* ==================== 解析层 ==================== */

int md_parse_ack(const uint8_t* payload, uint16_t len, md_ack_t* out)
{
    uint8_t ln;

    if (payload == nullptr || out == nullptr)
        return 0;

    /* 完整 ACK 帧模式：[AA][CMD][01][err][CRC] */
    if (len >= 5 && payload[0] == MD_SYNC) {
        ln = payload[2];
        if (ln != 1)
            return 0;
        if (md_crc8(payload + 1, 3) != payload[4])
            return 0;
        out->cmd = payload[1];
        out->err = payload[3];
        return 1;
    }

    /* DATA 段模式：仅 1 字节 err（此时 cmd 未知，置 0） */
    if (len < 1)
        return 0;
    out->cmd = 0;
    out->err = payload[0];
    return 1;
}

int md_parse_status(const uint8_t* payload, uint16_t len, md_status_t* out)
{
    uint16_t i;

    if (payload == nullptr || out == nullptr)
        return 0;

    if (len == 56) {    /* 常规模式 */
        for (i = 0; i < 4; i++)
            out->enc[i] = md_get_i32(payload + 0 + i * 4);
        for (i = 0; i < 4; i++)
            out->tgt[i] = md_get_f32(payload + 16 + i * 4);
        for (i = 0; i < 4; i++)
            out->rpm[i] = md_get_i32(payload + 32 + i * 4);
        for (i = 0; i < 4; i++)
            out->rpm_raw[i] = 0;
        out->sbus_frame_cnt = md_get_u32(payload + 48);
        out->sbus_ok_cnt    = md_get_u32(payload + 52);
        out->extended = 0;
        return 1;
    }
    if (len == 72) {    /* 扩展模式（DEBUG_SPEED=1） */
        for (i = 0; i < 4; i++)
            out->enc[i] = md_get_i32(payload + 0 + i * 4);
        for (i = 0; i < 4; i++)
            out->tgt[i] = md_get_f32(payload + 16 + i * 4);
        for (i = 0; i < 4; i++)
            out->rpm[i] = md_get_i32(payload + 32 + i * 4);
        for (i = 0; i < 4; i++)
            out->rpm_raw[i] = md_get_i32(payload + 48 + i * 4);
        out->sbus_frame_cnt = md_get_u32(payload + 64);
        out->sbus_ok_cnt    = md_get_u32(payload + 68);
        out->extended = 1;
        return 1;
    }
    return 0;
}

int md_parse_detect(const uint8_t* payload, uint16_t len, md_detect_t* out)
{
    if (payload == nullptr || out == nullptr || len < 6)
        return 0;

    out->proto = payload[0];
    out->inv   = payload[1];
    out->baud  = md_get_u32(payload + 2);
    return 1;
}

int md_parse_sbus(const uint8_t* payload, uint16_t len, uint16_t ch[16])
{
    uint16_t i;

    if (payload == nullptr || ch == nullptr || len < 32)
        return 0;

    for (i = 0; i < 16; i++)
        ch[i] = md_get_u16(payload + i * 2);
    return 1;
}

#if MD_ENABLE_CONFIG
int md_parse_config(const uint8_t* raw, uint16_t len, md_config_t* out)
{
    uint16_t i;

    if (raw == nullptr || out == nullptr || len < (uint16_t)MD_CONFIG_SIZE)
        return 0;

    out->baud_rate      = md_get_u32(raw + 11);
    out->cmd_timeout_ms = md_get_u16(raw + 15);
    out->protocol       = (uint8_t)(raw[17] & 0x0F);
    out->sbus_inv       = (uint8_t)((raw[17] >> 4) & 0x01);
    out->ctrl_priority  = (uint8_t)((raw[17] >> 5) & 0x01);
    for (i = 0; i < 4; i++)
        out->control_mode[i] = (uint8_t)((raw[18] >> (i * 2)) & 0x03);
    for (i = 0; i < 4; i++)
        out->motor_invert[i] = (uint8_t)((raw[19] >> (i * 2)) & 0x03);
    for (i = 0; i < 4; i++)
        out->encoder_cpr[i] = md_get_u16(raw + 20 + i * 2);
    for (i = 0; i < 4; i++)
        out->speed_period_ms[i] = md_get_u16(raw + 28 + i * 2);
    for (i = 0; i < 4; i++)
        out->speed_pid_type[i] = (uint8_t)((md_get_u16(raw + 36) >> (i * 4)) & 0x0F);
    for (i = 0; i < 4; i++)
        out->speed_olim[i] = md_get_u16(raw + 38 + i * 2);
    for (i = 0; i < 4; i++) {
        out->speed_kp[i]   = md_get_f32(raw + 46 + i * 16 + 0);
        out->speed_ki[i]   = md_get_f32(raw + 46 + i * 16 + 4);
        out->speed_kd[i]   = md_get_f32(raw + 46 + i * 16 + 8);
        out->speed_ilim[i] = md_get_f32(raw + 46 + i * 16 + 12);
    }
    for (i = 0; i < 4; i++)
        out->pos_period_ms[i] = md_get_u16(raw + 110 + i * 2);
    for (i = 0; i < 4; i++)
        out->pos_pid_type[i] = (uint8_t)((md_get_u16(raw + 118) >> (i * 4)) & 0x0F);
    for (i = 0; i < 4; i++) {
        out->pos_kp[i]   = md_get_f32(raw + 120 + i * 16 + 0);
        out->pos_ki[i]   = md_get_f32(raw + 120 + i * 16 + 4);
        out->pos_kd[i]   = md_get_f32(raw + 120 + i * 16 + 8);
        out->pos_ilim[i] = md_get_f32(raw + 120 + i * 16 + 12);
    }
    for (i = 0; i < 4; i++)
        out->pos_olim[i] = md_get_f32(raw + 184 + i * 4);
    for (i = 0; i < 4; i++)
        out->pos_angle_cpr[i] = md_get_u16(raw + 200 + i * 2);
    for (i = 0; i < 4; i++)
        out->speed_filter_type[i] = (uint8_t)((md_get_u16(raw + 208) >> (i * 4)) & 0x0F);
    for (i = 0; i < 4; i++)
        out->speed_filter_window[i] = raw[210 + i];
    for (i = 0; i < 4; i++)
        out->sbus_channel[i] = (uint8_t)(((md_get_u16(raw + 214) >> (i * 4)) & 0x0F) + 1);
    for (i = 0; i < 4; i++)
        out->rc_dir_ch[i] = (uint8_t)(((md_get_u16(raw + 216) >> (i * 4)) & 0x0F) + 1);
    for (i = 0; i < 4; i++)
        out->rc_map_mode[i] = (uint8_t)((raw[218] >> i) & 0x01);
    for (i = 0; i < 4; i++)
        out->rc_dir_en[i] = (uint8_t)((raw[218] >> (4 + i)) & 0x01);
    for (i = 0; i < 4; i++)
        out->sbus_param[i] = md_get_u16(raw + 219 + i * 2);
    out->sbus_range_min = md_get_u16(raw + 227);
    out->sbus_range_max = md_get_u16(raw + 229);

    return 1;
}

uint16_t md_pack_config(const md_config_t* cfg, uint8_t* out, uint16_t cap)
{
    uint16_t i;
    uint16_t v;

    if (cfg == nullptr || out == nullptr || cap < (uint16_t)MD_CONFIG_SIZE)
        return 0;

    /* 受保护区（offset 0~10：magic/hw_ver/sw_ver/_reserved/crc）置 0，
     * 固件写入时自动还原受保护字段 */
    for (i = 0; i < 11; i++)
        out[i] = 0;

    md_put_u32(out + 11, cfg->baud_rate);
    md_put_u16(out + 15, cfg->cmd_timeout_ms);
    out[17] = (uint8_t)((cfg->protocol & 0x0F)
                      | ((cfg->sbus_inv & 0x01) << 4)
                      | ((cfg->ctrl_priority & 0x01) << 5));

    out[18] = 0;
    for (i = 0; i < 4; i++)
        out[18] |= (uint8_t)((cfg->control_mode[i] & 0x03) << (i * 2));
    out[19] = 0;
    for (i = 0; i < 4; i++)
        out[19] |= (uint8_t)((cfg->motor_invert[i] & 0x03) << (i * 2));

    for (i = 0; i < 4; i++)
        md_put_u16(out + 20 + i * 2, cfg->encoder_cpr[i]);
    for (i = 0; i < 4; i++)
        md_put_u16(out + 28 + i * 2, cfg->speed_period_ms[i]);

    v = 0;
    for (i = 0; i < 4; i++)
        v |= (uint16_t)((cfg->speed_pid_type[i] & 0x0F) << (i * 4));
    md_put_u16(out + 36, v);

    for (i = 0; i < 4; i++)
        md_put_u16(out + 38 + i * 2, cfg->speed_olim[i]);

    for (i = 0; i < 4; i++) {
        md_put_f32(out + 46 + i * 16 + 0,  cfg->speed_kp[i]);
        md_put_f32(out + 46 + i * 16 + 4,  cfg->speed_ki[i]);
        md_put_f32(out + 46 + i * 16 + 8,  cfg->speed_kd[i]);
        md_put_f32(out + 46 + i * 16 + 12, cfg->speed_ilim[i]);
    }

    for (i = 0; i < 4; i++)
        md_put_u16(out + 110 + i * 2, cfg->pos_period_ms[i]);

    v = 0;
    for (i = 0; i < 4; i++)
        v |= (uint16_t)((cfg->pos_pid_type[i] & 0x0F) << (i * 4));
    md_put_u16(out + 118, v);

    for (i = 0; i < 4; i++) {
        md_put_f32(out + 120 + i * 16 + 0,  cfg->pos_kp[i]);
        md_put_f32(out + 120 + i * 16 + 4,  cfg->pos_ki[i]);
        md_put_f32(out + 120 + i * 16 + 8,  cfg->pos_kd[i]);
        md_put_f32(out + 120 + i * 16 + 12, cfg->pos_ilim[i]);
    }

    for (i = 0; i < 4; i++)
        md_put_f32(out + 184 + i * 4, cfg->pos_olim[i]);

    for (i = 0; i < 4; i++)
        md_put_u16(out + 200 + i * 2, cfg->pos_angle_cpr[i]);

    v = 0;
    for (i = 0; i < 4; i++)
        v |= (uint16_t)((cfg->speed_filter_type[i] & 0x0F) << (i * 4));
    md_put_u16(out + 208, v);

    for (i = 0; i < 4; i++)
        out[210 + i] = cfg->speed_filter_window[i];

    v = 0;
    for (i = 0; i < 4; i++) {
        uint16_t t = (cfg->sbus_channel[i] >= 1 && cfg->sbus_channel[i] <= 16)
                   ? (uint16_t)(cfg->sbus_channel[i] - 1) : 0;
        v |= (uint16_t)((t & 0x0F) << (i * 4));
    }
    md_put_u16(out + 214, v);

    v = 0;
    for (i = 0; i < 4; i++) {
        uint16_t t = (cfg->rc_dir_ch[i] >= 1 && cfg->rc_dir_ch[i] <= 16)
                   ? (uint16_t)(cfg->rc_dir_ch[i] - 1) : 0;
        v |= (uint16_t)((t & 0x0F) << (i * 4));
    }
    md_put_u16(out + 216, v);

    out[218] = 0;
    for (i = 0; i < 4; i++)
        out[218] |= (uint8_t)((cfg->rc_map_mode[i] & 0x01) << i);
    for (i = 0; i < 4; i++)
        out[218] |= (uint8_t)((cfg->rc_dir_en[i] & 0x01) << (4 + i));

    for (i = 0; i < 4; i++)
        md_put_u16(out + 219 + i * 2, cfg->sbus_param[i]);

    md_put_u16(out + 227, cfg->sbus_range_min);
    md_put_u16(out + 229, cfg->sbus_range_max);

    return MD_CONFIG_SIZE;
}
#endif /* MD_ENABLE_CONFIG */
