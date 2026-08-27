/**
 * @file    mdc_lite_ctrl.c
 * @brief   Motor Driver Controller 极简调用库 —— 回调接收器（pure C 实现）
 *
 * **独立实现**：本文件只依赖同族极简库 mdc_lite.h（md_lite_crc8 + 帧常量），
 * 自带**流式解析器**：滑窗找 0xAA 同步、CRC8 校验，仅关心 0xF0 STATUS_REPORT →
 * 提取 rpm[4] → 调用用户速度回调；其余帧一律丢弃。**不依赖完整库 mdc_lib**，
 * 不使用 md_parser_t / md_parse_status。
 *
 * 代码风格：C89 兼容（声明在块首、无 // 注释）。
 */

#include "mdc_lite_ctrl.h"

void md_lite_ctrl_init(md_lite_ctrl_t* c, md_lite_on_speed_t cb)
{
    if (c == 0)
        return;
    c->len = 0;
    c->on_speed = cb;
}

/* 丢弃当前缓冲首字节（视为假同步），并在剩余缓冲中重扫下一个 0xAA */
static void md_lite_drop_first(md_lite_ctrl_t* c)
{
    uint16_t i;

    if (c->len <= 1) {
        c->len = 0;
        return;
    }
    for (i = 1; i < c->len; i++) {
        if (c->buf[i] == MD_LITE_SYNC) {
            uint16_t j;
            for (j = 0; j + i < c->len; j++)
                c->buf[j] = c->buf[i + j];
            c->len = (uint16_t)(c->len - i);
            return;
        }
    }
    c->len = 0;
}

/* 从 0xF0 数据段（56B/72B，rpm 均位于偏移 32）解析四通道 rpm（int32 LE）。
 * 与 mdc_lib 的 md_parse_status 在偏移 0x20 处取 rpm[4] 完全一致。 */
static int md_lite_parse_rpm(const uint8_t* payload, uint16_t plen, int32_t rpm[4])
{
    uint16_t i;

    if (plen != MD_LITE_STATUS_LEN_56 && plen != MD_LITE_STATUS_LEN_72)
        return 0;
    for (i = 0; i < 4; i++) {
        uint32_t v = (uint32_t)payload[32 + i * 4]
                   | ((uint32_t)payload[33 + i * 4] << 8)
                   | ((uint32_t)payload[34 + i * 4] << 16)
                   | ((uint32_t)payload[35 + i * 4] << 24);
        rpm[i] = (int32_t)v;
    }
    return 1;
}

void md_lite_ctrl_feed(md_lite_ctrl_t* c, uint8_t byte)
{
    uint8_t  ln;
    uint16_t need;
    uint8_t  cmd;
    uint16_t plen;
    int32_t  rpm[4];

    if (c == 0)
        return;

    /* 空闲状态：只认同步字 0xAA（文本回显等噪声在此被丢弃） */
    if (c->len == 0) {
        if (byte == MD_LITE_SYNC) {
            c->buf[0] = byte;
            c->len = 1;
        }
        return;
    }

    /* 缓冲已满（噪声堆积或非法长帧）：丢首字节腾位 */
    if (c->len >= MD_LITE_BUF_SIZE)
        md_lite_drop_first(c);

    c->buf[c->len] = byte;
    c->len++;

    /* 至少需要 CMD + LEN 才能判断帧长 */
    if (c->len < 3)
        return;

    ln = c->buf[2];
    if (ln > MD_LITE_MAX_DATA) {        /* LEN 非法：丢弃该帧起点，重扫 */
        md_lite_drop_first(c);
        return;
    }

    need = (uint16_t)(3 + ln + 1);      /* CMD + LEN + DATA + CRC */
    if (c->len < need)
        return;

    if (md_lite_crc8(c->buf + 1, (uint16_t)(ln + 2)) == c->buf[need - 1]) {
        /* CRC 通过：取出一帧。在重置缓冲前先消费 cmd / plen（帧内地址） */
        cmd = c->buf[1];
        plen = ln;
        c->len = 0;

        if (cmd == MD_LITE_CMD_STATUS_REPORT && c->on_speed != 0) {
            /* 56B / 72B 自动兼容；rpm 在栈上，回调在本次调用内同步消费 */
            if (md_lite_parse_rpm(c->buf + 3, plen, rpm))
                c->on_speed(rpm);
        }
        /* 其它命令帧（0x31 控制帧、ACK 等）或噪声：不触发回调，静默丢弃 */
    } else {
        /* CRC 失败：丢弃该帧起点，继续滑动扫描 */
        md_lite_drop_first(c);
    }
}
