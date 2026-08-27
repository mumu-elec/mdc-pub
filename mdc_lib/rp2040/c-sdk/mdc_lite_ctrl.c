/**
 * @file    mdc_lite_ctrl.c
 * @brief   Motor Driver Controller 极简调用库 —— 回调接收器（pure C 实现）
 *
 * 复用 mdc_lib 的 md_parser_t / md_parser_init / md_parser_feed / md_parse_status，
 * 只关心 0xF0 STATUS_REPORT → 提取 rpm[4] → 调用用户速度回调；其余帧一律按
 * mdc_lib 协议丢弃。本文件不 include 任何硬件头文件。
 *
 * 代码风格：与 mdc_lib.c 一致（C89 兼容：声明在块首、无 // 注释）。
 */

#include "mdc_lite_ctrl.h"

void md_lite_ctrl_init(md_lite_ctrl_t* c, md_lite_on_speed_t cb)
{
    if (c == 0)
        return;
    md_parser_init(&c->parser);
    c->on_speed = cb;
}

void md_lite_ctrl_feed(md_lite_ctrl_t* c, uint8_t byte)
{
    uint8_t        cmd;
    const uint8_t* payload;
    uint16_t       plen;
    md_status_t    st;

    if (c == 0)
        return;

    /* 无论是否派发回调，都必须喂解析器以推进流式状态机（滑窗找 0xAA + CRC 校验） */
    if (md_parser_feed(&c->parser, byte, &cmd, &payload, &plen)) {
        if (cmd == MD_CMD_STATUS_REPORT && c->on_speed != 0) {
            /* 56B / 72B 自动兼容；st 在栈上，回调在本次调用内同步消费 */
            if (md_parse_status(payload, plen, &st))
                c->on_speed(st.rpm);
        }
        /* 其它命令帧（0x31 控制帧、ACK 等）或噪声：不触发回调，静默丢弃 */
    }
}
