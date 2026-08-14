/*
 * ============================================================================
 *  motor_driver.c — Motor Driver Controller 协议封装层实现（STM32 HAL 例程）
 * ============================================================================
 *  纯函数 + 依赖注入回调：本文件不直接调用 HAL，UART 发送通过回调
 *  motor_driver_send_uart() 完成（由用户在 main_example.c 中实现）。
 *
 *  协议依据：common/协议规范.md §3
 *    - 帧格式：[SYNC=0xAA][CMD:1B][LEN:1B][DATA:0~250B][CRC8:1B]
 *    - CRC8：多项式 0x07，初值 0，计算范围 = CMD + LEN + DATA（不含 SYNC）
 *    - 多字节字段一律小端序（LE）
 * ============================================================================
 */
#include "motor_driver.h"
#include <string.h>

/* CRC8：多项式 0x07，初值 0，计算范围 = CMD + LEN + DATA（不含 SYNC）。
 * 严格按协议规范 §3.1 参考实现移植。 */
uint8_t md_crc8(const uint8_t *d, uint16_t len)
{
    uint8_t c = 0;
    uint16_t i;
    uint8_t b;

    for (i = 0; i < len; i++) {
        c ^= d[i];
        for (b = 0; b < 8; b++)
            c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
    }
    return c;
}

/* 组帧：[0xAA][CMD][LEN][DATA...][CRC8]，返回帧总长度 = len + 4 */
uint16_t md_build_frame(uint8_t cmd, const uint8_t *data, uint8_t len, uint8_t *out)
{
    uint16_t idx = 0;

    out[idx++] = MD_SYNC;
    out[idx++] = cmd;
    out[idx++] = len;
    if (data != NULL && len > 0) {
        memcpy(&out[idx], data, len);
        idx += len;
    }
    /* CRC8 计算范围 = CMD + LEN + DATA（跳过 SYNC） */
    out[idx] = md_crc8(&out[1], (uint16_t)(idx - 1));
    return (uint16_t)(idx + 1);
}

/* 发送一帧（组帧后经依赖注入回调发出） */
static void md_send_frame(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    uint8_t buf[256];
    uint16_t n = md_build_frame(cmd, data, len, buf);

    motor_driver_send_uart(buf, n);
}

/* 发送文本指令（规范 §2.1：以换行结尾；带参数=写入，不带参数=读取） */
void md_send_text(const char *cmd)
{
    uint8_t buf[128];
    uint16_t n = 0;

    while (cmd[n] != '\0' && n < sizeof(buf) - 1) {
        buf[n++] = (uint8_t)cmd[n];
    }
    buf[n++] = '\n';                     /* 指令必须以 '\n' 结尾 */
    motor_driver_send_uart(buf, n);
}

/* int32 小端手动拼装（逐字节移位，避免依赖 union/结构体对齐） */
static void put_i32_le(uint8_t *buf, uint16_t *idx, int32_t v)
{
    buf[(*idx)++] = (uint8_t)(v & 0xFF);
    buf[(*idx)++] = (uint8_t)((v >> 8) & 0xFF);
    buf[(*idx)++] = (uint8_t)((v >> 16) & 0xFF);
    buf[(*idx)++] = (uint8_t)((v >> 24) & 0xFF);
}

/* 发送 0x31 MOTOR_CTRL：DATA = [m1~m4: 4×int32 LE]（16B）。
 * 实时控制以 30/50/100ms 间隔连续发送（规范 §3.3）。 */
void md_motor_ctrl(const int32_t targets[4])
{
    uint8_t data[16];
    uint16_t idx = 0;
    int i;

    for (i = 0; i < 4; i++) {
        put_i32_le(data, &idx, targets[i]);
    }
    md_send_frame(MD_CMD_MOTOR_CTRL, data, 16);
}

/* 发送 0x40 SUBSCRIBE：DATA = [interval_ms:2B LE]（最低 20ms） */
void md_subscribe(uint16_t interval_ms)
{
    uint8_t data[2];

    data[0] = (uint8_t)(interval_ms & 0xFF);
    data[1] = (uint8_t)((interval_ms >> 8) & 0xFF);
    md_send_frame(MD_CMD_SUBSCRIBE, data, 2);
}

/* 发送 0x10 READ_PARAM：读取全部配置（应答 231B config_t） */
void md_read_param(void)
{
    md_send_frame(MD_CMD_READ_PARAM, NULL, 0);
}

/* 发送 0x20 SAVE_EEPROM：RAM 配置写入 EEPROM（约 190ms） */
void md_save_eeprom(void)
{
    md_send_frame(MD_CMD_SAVE_EEPROM, NULL, 0);
}

/* ------------------------------------------------------------------ */
/* 接收解析：滑动窗口状态机                                            */
/*   每收到一个字节调用 md_rx_byte(b)，内部按                          */
/*   SYNC → CMD → LEN → DATA → CRC 顺序解析；CRC 校验通过后分发。     */
/* ------------------------------------------------------------------ */
typedef enum {
    MD_RX_WAIT_SYNC = 0,
    MD_RX_CMD,
    MD_RX_LEN,
    MD_RX_DATA,
    MD_RX_CRC
} md_rx_state_t;

static md_rx_state_t s_rx_state = MD_RX_WAIT_SYNC;
static uint8_t s_rx_cmd;                 /* 当前帧 CMD */
static uint8_t s_rx_len;                 /* 当前帧 LEN（DATA 长度） */
static uint8_t s_rx_buf[256];            /* 帧缓冲：[0]=CMD [1]=LEN [2..]=DATA */
static uint16_t s_rx_idx;                /* 缓冲写入位置 */

/* 弱实现宏：兼容 GCC / IAR / ARMCC */
#ifndef MD_WEAK
#if defined(__GNUC__)
#define MD_WEAK __attribute__((weak))
#elif defined(__ICCARM__)
#define MD_WEAK __weak
#else
#define MD_WEAK
#endif
#endif

/* 收到一帧完整数据（CMD + DATA，CRC 已校验通过） */
static void md_dispatch(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    if (cmd == MD_CMD_STATUS_REPORT) {
        /* 0xF0：周期状态上报，用户可重写此回调解析（规范 §4） */
        md_on_status_report(data, len);
    } else if (cmd == MD_CMD_READ_PARAM) {
        /* 0x10 应答：config_t (231B)，用户可重写此回调解析（规范 §5） */
        md_on_read_param(data, (uint16_t)len);
    }
    /* 其它帧（0x01 PING ACK、0x40 SUBSCRIBE ACK 等）按需扩展 */
}

void md_rx_byte(uint8_t b)
{
    switch (s_rx_state) {
    case MD_RX_WAIT_SYNC:
        if (b == MD_SYNC) {
            s_rx_state = MD_RX_CMD;
        }
        break;

    case MD_RX_CMD:
        s_rx_cmd = b;
        s_rx_state = MD_RX_LEN;
        break;

    case MD_RX_LEN:
        s_rx_len = b;
        s_rx_buf[0] = s_rx_cmd;          /* 为 CRC 校验准备连续缓冲：CMD+LEN+DATA */
        s_rx_buf[1] = b;
        s_rx_idx = 2;
        s_rx_state = (s_rx_len == 0) ? MD_RX_CRC : MD_RX_DATA;
        break;

    case MD_RX_DATA:
        if (s_rx_idx < sizeof(s_rx_buf)) {
            s_rx_buf[s_rx_idx++] = b;
        }
        if (s_rx_idx >= (uint16_t)(s_rx_len + 2)) {
            s_rx_state = MD_RX_CRC;
        }
        break;

    case MD_RX_CRC: {
        /* CRC8 计算范围 = CMD + LEN + DATA（不含 SYNC），长度 = 2 + len */
        uint8_t calc = md_crc8(s_rx_buf, (uint16_t)(s_rx_len + 2));

        if (b == calc) {
            md_dispatch(s_rx_buf[0], &s_rx_buf[2], s_rx_len);
        }
        /* 滑动窗口：无论成败回到找 SYNC；若本字节恰好是 0xAA 可作下一帧同步头 */
        s_rx_state = (b == MD_SYNC) ? MD_RX_CMD : MD_RX_WAIT_SYNC;
        break;
    }

    default:
        s_rx_state = MD_RX_WAIT_SYNC;
        break;
    }
}

/* ------------------------- 默认回调（弱实现） --------------------------- */

/* 0xF0 STATUS_REPORT 解析骨架（规范 §4）：
 *   常规模式（DEBUG_SPEED=0）56B payload：
 *     [enc1~4:4×int32 LE][tgt1~4:4×float LE][rpm1~4:4×int32 LE]
 *     [sbus_frame_cnt:4B LE][sbus_ok_cnt:4B LE]
 *   扩展模式（DEBUG_SPEED=1）72B payload：末尾追加 [rpm_raw1~4:4×int32 LE]。
 * 默认实现不打印（避免依赖 printf），用户可在 main_example.c 中重写。 */
MD_WEAK void md_on_status_report(const uint8_t *data, uint8_t len)
{
    (void)data;
    (void)len;
}

/* 0x10 READ_PARAM 应答解析骨架（规范 §5，config_t = 231B）：
 *   可写字段偏移参考：
 *     baud_rate          offset 11 (4B u32)
 *     cmd_timeout_ms     offset 15 (2B u16)
 *     comm_flags         offset 17 (1B u8)
 *     control_mode       offset 18 (1B u8, 每电机 2bit: 0=开环 1=速度 2=位置)
 *     encoder_cpr[4]     offset 20 (8B u16×4)
 *     speed_ctrl_params  offset 46 (64B f32×16)
 *   默认实现不打印，用户可在 main_example.c 中重写。 */
MD_WEAK void md_on_read_param(const uint8_t *cfg, uint16_t len)
{
    (void)cfg;
    (void)len;
}
