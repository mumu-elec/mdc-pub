/**
 * @file    mdc_lite_ctrl.cpp
 * @brief   Motor Driver Controller 极简调用库 —— 调用+回调接收（control + speed callback）实现
 *
 * 规范依据：../LITE.md（mdc_lite 极简 LITE API）
 * 复用 mdc_lib 的流式解析器 md_parser_feed 与状态解析 md_parse_status，只新增"回调派发"。
 * 本文件不 include 任何 Arduino / ESP 头文件（纯 C++，C++11 兼容）。
 *
 * 四平台（esp32 / rp2040 / avr / esp8266）此文件内容完全一致。
 */

#include "mdc_lite_ctrl.h"

void md_lite_ctrl_init(md_lite_ctrl_t* self, md_lite_on_speed_t cb)
{
    if (self == nullptr)
        return;
    md_parser_init(&self->parser);
    self->on_speed = cb;
}

void md_lite_ctrl_feed(md_lite_ctrl_t* self, uint8_t byte)
{
    uint8_t        cmd;
    const uint8_t* payload;
    uint16_t       plen;

    if (self == nullptr)
        return;

    /* 流式解析：自动找 0xAA 同步 + CRC 校验，完整帧返回 1。
     * payload 指向 self->parser.buf 内部，须在下一次 feed 前消费 */
    if (md_parser_feed(&self->parser, byte, &cmd, &payload, &plen)) {
        /* 仅 0xF0 STATUS_REPORT 触发回调；其他帧（0x31/ACK/文本回显等）忽略 */
        if (cmd == MD_CMD_STATUS_REPORT && self->on_speed != nullptr) {
            md_status_t st;
            /* 56B 常规 / 72B 扩展自动兼容；md_parse_status 立即拷贝到 st，
             * 因此在下次 feed 前同步调用回调是安全的 */
            if (md_parse_status(payload, plen, &st) == 1)
                self->on_speed(st.rpm);
        }
    }
}
