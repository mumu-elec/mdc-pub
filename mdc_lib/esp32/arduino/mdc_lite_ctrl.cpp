/**
 * @file    mdc_lite_ctrl.cpp
 * @brief   Motor Driver Controller 极简调用库 —— 调用+回调接收（control + speed callback，独立实现）
 *
 * 规范依据：../LITE.md（mdc_lite 极简 LITE API）
 *
 * **独立实现**：本文件自带**滑窗流式解析器**（自动找 0xAA 同步 + CRC8 校验 + 逐字节
 * 流式推进），**不依赖**本目录 mdc_lib.h / mdc_lib.cpp（不调用 md_parser_feed /
 * md_parse_status），只复用同目录 mdc_lite.h 的发送侧与 CRC8。
 * 仅 0xF0 STATUS_REPORT（56B/72B 自动兼容）触发速度回调，其余帧/噪声静默丢弃。
 *
 * 本文件不 include 任何 Arduino / ESP 头文件（纯 C++，C++11 兼容）。
 *
 * 四平台（esp32 / rp2040 / avr / esp8266）此文件内容完全一致。
 */

#include "mdc_lite_ctrl.h"

/* ==================== 内部工具：小端手动拆解（16 位 int 平台如 AVR 上行为一致） ==================== */

static inline int32_t md_lite_get_i32(const uint8_t* p)
{
    uint32_t u = (uint32_t)p[0]
               | ((uint32_t)p[1] << 8)
               | ((uint32_t)p[2] << 16)
               | ((uint32_t)p[3] << 24);
    return (int32_t)u;
}

/* 丢弃当前缓冲首字节（视为假同步），并在剩余缓冲中重扫下一个 0xAA */
static inline void md_lite_parser_drop_first(md_lite_ctrl_t* p)
{
    uint16_t i;

    if (p->len <= 1) {
        p->len = 0;
        return;
    }
    for (i = 1; i < p->len; i++) {
        if (p->buf[i] == MD_SYNC) {
            for (uint16_t j = 0; j + i < p->len; j++)
                p->buf[j] = p->buf[j + i];
            p->len = (uint16_t)(p->len - i);
            return;
        }
    }
    p->len = 0;
}

/* 仅 0xF0（56B/72B）解析四通道 rpm 并调用回调；其余不触发 */
static inline void md_lite_dispatch_status(md_lite_ctrl_t* self,
                                           const uint8_t* payload, uint16_t plen)
{
    int32_t rpm[4];

    if (self->on_speed == nullptr)
        return;
    if (plen != 56 && plen != 72)
        return;

    /* rpm 在 STATUS_REPORT payload 偏移 32 处，4×int32 LE（56B/72B 布局相同） */
    rpm[0] = md_lite_get_i32(payload + 32 + 0);
    rpm[1] = md_lite_get_i32(payload + 32 + 4);
    rpm[2] = md_lite_get_i32(payload + 32 + 8);
    rpm[3] = md_lite_get_i32(payload + 32 + 12);
    self->on_speed(rpm);
}

void md_lite_ctrl_init(md_lite_ctrl_t* self, md_lite_on_speed_t cb)
{
    if (self == nullptr)
        return;
    self->len = 0;
    self->on_speed = cb;
}

void md_lite_ctrl_feed(md_lite_ctrl_t* self, uint8_t byte)
{
    uint8_t  ln;
    uint16_t need;

    if (self == nullptr)
        return;

    /* 空闲状态：只认同步字 0xAA（文本回显等噪声在此被丢弃） */
    if (self->len == 0) {
        if (byte == MD_SYNC) {
            self->buf[0] = byte;
            self->len = 1;
        }
        return;
    }

    /* 缓冲已满（MDC_LITE_PARSER_BUF 过小或噪声堆积）：丢首字节腾位 */
    if (self->len >= (uint16_t)MDC_LITE_PARSER_BUF)
        md_lite_parser_drop_first(self);

    self->buf[self->len] = byte;
    self->len++;

    /* 至少需要 CMD + LEN 才能判断帧长 */
    if (self->len < 3)
        return;

    ln = self->buf[2];
    if (ln > (uint16_t)MD_MAX_DATA) {   /* LEN 非法：丢弃该帧起点，重扫 */
        md_lite_parser_drop_first(self);
        return;
    }

    need = (uint16_t)(3 + ln + 1);      /* CMD + LEN + DATA + CRC */
    if (self->len < need)
        return;

    if (md_lite_crc8(self->buf + 1, (uint16_t)(ln + 2)) == self->buf[need - 1]) {
        /* CRC 通过：仅 0xF0 STATUS_REPORT（56B/72B）触发回调，其余帧/噪声忽略。
         * payload 指向 self->buf+3 内部，本函数内同步消费（回调整体拷贝 rpm），
         * 因此在下次 feed 覆写前调用是安全的 */
        if (self->buf[1] == MD_CMD_STATUS_REPORT)
            md_lite_dispatch_status(self, self->buf + 3, ln);
        self->len = 0;
        return;
    }

    /* CRC 失败：丢弃该帧起点，继续滑动扫描 */
    md_lite_parser_drop_first(self);
}
