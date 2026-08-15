/**
 * @file    mdc_lib.h
 * @brief   Motor Driver Controller generic library (8051 / Keil C51 port)
 *
 * Protocol basis: ../../../../例程/common/协议规范.md (layout v2.1, config_t = 231B)
 * API spec:       ../../API.md (one unified md_* signature set for all platforms)
 *
 * Positioning: pack-only / parse-only. This library NEVER touches the UART or
 * any hardware peripheral; the user implements TX/RX with Keil C51 (e.g. the
 * 8051 on-chip UART) and feeds received bytes into md_parser_feed().
 *
 * Keil C51 notes:
 *   - Written in C89 style (no // comments, no VLA, declarations at block
 *     start, English comments only, so the file is encoding-safe in Keil).
 *   - Multi-byte fields are little-endian (LE), assembled by explicit shifts
 *     (16-bit int of C51 is handled by widening to unsigned first).
 *   - Large arrays MUST live in xdata on 8051 (internal RAM is only 128B in
 *     SMALL model). Declare the parser instance as:
 *         xdata md_parser_t g_parser;     (MD_PARSER_BUF bytes of xdata)
 *     and any config buffer as xdata too. See README.md for RAM usage.
 *   - Set MD_ENABLE_CONFIG=0 to compile out the config full-field functions
 *     (md_pack_config / md_parse_config / md_bin_write_param) and the 231B
 *     xdata scratch buffer, saving RAM and code.
 *
 * GCC syntax check: the source uses MD_51_XDATA (expands to the real xdata
 * keyword on Keil/SDCC via __C51__/__SDCC__ detection, empty elsewhere), so
 * a plain  gcc -std=c89 -fsyntax-only mdc_lib.c  works; passing -Dxdata=
 * also works as an alternative. See mdc_lib.c and README.md.
 */

#ifndef MDC_LIB_H
#define MDC_LIB_H

#include <stdint.h>     /* Keil C51 provides <stdint.h> */

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Constants (API.md 2.5, same on all platforms) ==================== */

#define MD_SYNC         0xAAu    /* binary frame sync byte */
#define MD_MAX_DATA     250u     /* max DATA segment length */
#define MD_CONFIG_SIZE  231u     /* config_t size */
#define MD_FRAME_MAX    235u     /* max whole frame length (4 + 231) */
#define MD_CRC8_POLY    0x07u    /* CRC8 polynomial (init 0, bitwise) */
#define MD_CMD_PING     0x01u    /* ping */
#define MD_ERR_OK       0x00u    /* ACK ok */
#define MD_ERR_FAIL     0xFFu    /* ACK fail */

/* Binary command numbers (protocol spec 3.3) */
#define MD_CMD_READ_PARAM    0x10u   /* read all config (reply: config_t 231B) */
#define MD_CMD_WRITE_PARAM   0x11u   /* write all config (RAM only) */
#define MD_CMD_WRITE_FIELD   0x12u   /* write single field by offset */
#define MD_CMD_SAVE_EEPROM   0x20u   /* RAM -> EEPROM */
#define MD_CMD_LOAD_EEPROM   0x21u   /* EEPROM -> RAM */
#define MD_CMD_FACTORY_RESET 0x22u   /* factory reset */
#define MD_CMD_MOTOR_RAW     0x30u   /* single channel PWM direct drive */
#define MD_CMD_MOTOR_CTRL    0x31u   /* 4-channel batch control (core frame) */
#define MD_CMD_SUBSCRIBE     0x40u   /* start periodic status report */
#define MD_CMD_UNSUBSCRIBE   0x41u   /* stop status report */
#define MD_CMD_DEBUG_SBUS    0x43u   /* SBUS channel report on/off */
#define MD_CMD_DEBUG_SPEED   0x44u   /* raw speed report on/off (0xF0 -> 72B) */
#define MD_CMD_ENTER_BL      0x52u   /* soft reset into bootloader */
#define MD_CMD_REBOOT        0x53u   /* system reboot */
#define MD_CMD_STATUS_REPORT 0xF0u   /* status report (MCU push) */
#define MD_CMD_DETECT_REPORT 0xF1u   /* detect result report (MCU push) */
#define MD_CMD_SBUS_DATA     0xF2u   /* SBUS 16-channel raw report (MCU push) */

/* Streaming parser buffer size (shrinkable). Default 256 fits the largest
 * frame (235B). On 8051 you may shrink it, but below 235 the READ_PARAM
 * 231B reply cannot be fully parsed. See README.md for RAM trade-offs. */
#ifndef MD_PARSER_BUF
#define MD_PARSER_BUF 256u
#endif

/* Config full-field function switch (default 1).
 * Set to 0 on small-memory 8051 to compile out md_pack_config /
 * md_parse_config / md_bin_write_param and the 231B xdata scratch. */
#ifndef MD_ENABLE_CONFIG
#define MD_ENABLE_CONFIG 1
#endif

/* ==================== Data structures ==================== */

/* STATUS_REPORT (0xF0) parse result, see API.md 6.2.
 * 56B normal: enc/tgt/rpm + sbus_frame_cnt + sbus_ok_cnt
 * 72B extended: plus rpm_raw[4] (needs DEBUG_SPEED=1) */
typedef struct {
    int32_t  enc[4];          /* @0   encoder accumulated pulses */
    float    tgt[4];          /* @16  current targets */
    int32_t  rpm[4];          /* @32  filtered RPM */
    int32_t  rpm_raw[4];      /* @48  raw RPM before filter (72B only; 0 in 56B) */
    uint32_t sbus_frame_cnt;  /* 56B:@48 / 72B:@64 */
    uint32_t sbus_ok_cnt;     /* 56B:@52 / 72B:@68 */
    uint8_t  extended;        /* 1 = 72B extended mode */
} md_status_t;

/* DETECT_REPORT (0xF1) parse result: proto 0=fail 1=SBUS 2=UART 3=ELRS */
typedef struct {
    uint8_t  proto;
    uint8_t  inv;
    uint32_t baud;            /* LE 4B */
} md_detect_t;

/* ACK parse result: err=0x00 ok, any non-zero = fail */
typedef struct {
    uint8_t cmd;              /* valid in whole-frame mode; 0 in DATA-only mode */
    uint8_t err;
} md_ack_t;

/* config_t full-field structure (API.md 6.5).
 * Bit-field conventions: control_mode/motor_invert 2 bits per motor;
 * speed_pid_type/pos_pid_type/speed_filter_type 4 bits per motor;
 * sbus_channel/rc_dir_ch stored 0~15 = CH1~16, struct holds 1~16
 * (parse +1 / pack -1); rc_map_mode 1 bit per motor, rc_dir_en from
 * the same byte bits 4-7. */
typedef struct {
    /* comm */
    uint32_t baud_rate;          /* @11 USART2 baud */
    uint16_t cmd_timeout_ms;     /* @15 timeout protection (ms) */
    uint8_t  protocol;           /* @17 bit0-3: 1=SBUS 2=UART 3=ELRS */
    uint8_t  sbus_inv;           /* @17 bit4 */
    uint8_t  ctrl_priority;      /* @17 bit5: 0=USART2 first 1=USB first */
    /* motors x4 */
    uint8_t  control_mode[4];    /* @18 2bit/motor: 0 open 1 speed 2 position */
    uint8_t  motor_invert[4];    /* @19 2bit/motor: bit0 pin invert bit1 encoder polarity */
    uint16_t encoder_cpr[4];     /* @20 encoder lines */
    uint16_t speed_period_ms[4]; /* @28 speed loop period */
    uint8_t  speed_pid_type[4];  /* @36 4bit/motor: 0 positional 1 incremental */
    uint16_t speed_olim[4];      /* @38 speed output limit (PWM 0~1000) */
    float    speed_kp[4];        /* @46+ speed Kp */
    float    speed_ki[4];        /*      speed Ki */
    float    speed_kd[4];        /*      speed Kd */
    float    speed_ilim[4];      /*      speed integral limit */
    uint16_t pos_period_ms[4];   /* @110 position loop period */
    uint8_t  pos_pid_type[4];    /* @118 4bit/motor */
    float    pos_kp[4];          /* @120+ position Kp */
    float    pos_ki[4];          /*       position Ki */
    float    pos_kd[4];          /*       position Kd */
    float    pos_ilim[4];        /*       position integral limit */
    float    pos_olim[4];        /* @184 position output limit (RPM) */
    uint16_t pos_angle_cpr[4];   /* @200 pulses per revolution (0 = use encoder_cpr) */
    uint8_t  speed_filter_type[4];   /* @208 4bit/motor: 0 none 1 moving avg 2 LPF 3 median */
    uint8_t  speed_filter_window[4]; /* @210 filter window */
    uint8_t  sbus_channel[4];    /* @214 RC channel map (1~16; pack -1 to 0~15) */
    uint8_t  rc_dir_ch[4];       /* @216 direction map channel (1~16) */
    uint8_t  rc_map_mode[4];     /* @218 bit0-3 1bit/motor: 0 center zero 1 min zero */
    uint8_t  rc_dir_en[4];       /* @218 bit4-7 1bit/motor */
    uint16_t sbus_param[4];      /* @219 RC travel */
    uint16_t sbus_range_min;     /* @227 channel lower bound */
    uint16_t sbus_range_max;     /* @229 channel upper bound */
} md_config_t;

/* Streaming parser state (API.md 3.4).
 * The buffer lives inside this struct: on 8051 declare the instance in xdata,
 * e.g.  xdata md_parser_t g_parser;   (MD_PARSER_BUF bytes of xdata) */
typedef struct {
    uint8_t  buf[MD_PARSER_BUF];
    uint16_t len;
} md_parser_t;

/* ==================== Low level: CRC / frame build / frame parse ==================== */

/* CRC8-ATM: poly 0x07, init 0, bitwise.
 * Check vectors: crc8({0x01,0x00})==0x15; crc8("123456789")==0xF4 */
uint8_t md_crc8(const uint8_t* data, uint16_t len);

/* Build frame: [0xAA][CMD][LEN][DATA...][CRC8]; CRC covers CMD+LEN+DATA
 * (SYNC excluded). Returns bytes written; 0 if data_len>MD_MAX_DATA or cap
 * too small. Check vector: md_build_frame(0x01,NULL,0) == {AA 01 00 15} */
uint16_t md_build_frame(uint8_t cmd, const uint8_t* data, uint16_t data_len,
                        uint8_t* out, uint16_t cap);

/* Frame-level parse: checks SYNC and CRC. Returns 1 and fills cmd/payload/
 * payload_len on success (payload points into frame, consume before reuse);
 * returns 0 on failure. */
int md_parse_frame(const uint8_t* frame, uint16_t len,
                   uint8_t* cmd, const uint8_t** payload, uint16_t* payload_len);

/* ==================== Streaming parser ==================== */

void md_parser_init(md_parser_t* p);

/* Feed one byte. Returns 1 when a complete CRC-valid frame is available and
 * fills cmd/payload/payload_len, otherwise 0. Sliding window finds 0xAA;
 * LEN>250 or CRC failure drops the candidate sync and rescans.
 * NOTE: payload points into p->buf; copy it out before the next feed().
 * Text echo lines are discarded as noise (bytes before 0xAA are ignored). */
int md_parser_feed(md_parser_t* p, uint8_t byte,
                   uint8_t* cmd, const uint8_t** payload, uint16_t* payload_len);

/* ==================== Text command layer ==================== */

/* Generic builder: cmd="/mode", args="1 speed" -> "/mode 1 speed\n";
 * args=NULL -> "/mode\n". Writes '\n' and NUL; returns bytes excluding NUL;
 * returns 0 if cap is too small. */
uint16_t md_text_build(const char* cmd, const char* args, char* out, uint16_t cap);

uint16_t md_text_version(char* out, uint16_t cap);   /* /version\n */
uint16_t md_text_help(char* out, uint16_t cap);      /* /help\n */
uint16_t md_text_status(char* out, uint16_t cap);    /* /status\n */
uint16_t md_text_check(char* out, uint16_t cap);     /* /check\n */
uint16_t md_text_detect(char* out, uint16_t cap);    /* /detect\n */
uint16_t md_text_save(char* out, uint16_t cap);      /* /save\n */
uint16_t md_text_load(char* out, uint16_t cap);      /* /load\n */
uint16_t md_text_reset(char* out, uint16_t cap);     /* /reset\n */
uint16_t md_text_enczero(uint8_t ch, char* out, uint16_t cap);  /* /enczero 1\n (ch 1~4) */
uint16_t md_text_mode(uint8_t ch, const char* mode, char* out, uint16_t cap);
/* mode=NULL -> "/mode 1\n"; mode="speed" -> "/mode 1 speed\n" (ch 1~4) */

/* Other commands (speedctrl/posctrl/cpr/inv/einv/posangle/filter/uart2/
 * priority/timeout/smap/rmap/dmap/sbusparam/sbusrange) are built with
 * md_text_build, e.g.:
 *   md_text_build("/speedctrl", "1 0.5 0.02 0.01 500 800 10 0", out, cap);
 *   md_text_build("/uart2",     "115200 0 uart", out, cap);
 *   md_text_build("/sbusrange", "172 1811", out, cap); */

/* ==================== Binary command layer (15 packers) ==================== */

uint16_t md_bin_ping(uint8_t* out, uint16_t cap);            /* 0x01 */
uint16_t md_bin_read_param(uint8_t* out, uint16_t cap);      /* 0x10 */
#if MD_ENABLE_CONFIG
uint16_t md_bin_write_param(const md_config_t* cfg, uint8_t* out, uint16_t cap); /* 0x11 */
#endif
uint16_t md_bin_write_field(uint16_t field_id, const uint8_t* value,
                            uint16_t value_len, uint8_t* out, uint16_t cap);     /* 0x12 */
uint16_t md_bin_save(uint8_t* out, uint16_t cap);            /* 0x20 */
uint16_t md_bin_load(uint8_t* out, uint16_t cap);            /* 0x21 */
uint16_t md_bin_factory_reset(uint8_t* out, uint16_t cap);   /* 0x22 */
uint16_t md_bin_motor_raw(uint8_t ch, uint8_t dir, uint16_t pwm,
                          uint8_t* out, uint16_t cap);       /* 0x30 ch=0~3 dir=0fwd/1rev pwm=0~1000 */
uint16_t md_bin_motor_ctrl(int32_t t0, int32_t t1, int32_t t2, int32_t t3,
                           uint8_t* out, uint16_t cap);      /* 0x31 4x int32 LE */
uint16_t md_bin_subscribe(uint16_t interval_ms, uint8_t* out, uint16_t cap); /* 0x40 FW clamps >=20ms */
uint16_t md_bin_unsubscribe(uint8_t* out, uint16_t cap);     /* 0x41 */
uint16_t md_bin_debug_sbus(uint8_t enable, uint8_t* out, uint16_t cap);       /* 0x43 */
uint16_t md_bin_debug_speed(uint8_t enable, uint8_t* out, uint16_t cap);      /* 0x44 */
uint16_t md_bin_enter_bl(uint8_t* out, uint16_t cap);        /* 0x52 */
uint16_t md_bin_reboot(uint8_t* out, uint16_t cap);          /* 0x53 */

/* ==================== Parse layer ==================== */

/* Parse ACK: input may be the ACK DATA segment (1-byte err) or the whole ACK
 * frame (AA CMD 01 err CRC, auto-detected and verified). Returns 1/0. */
int md_parse_ack(const uint8_t* payload, uint16_t len, md_ack_t* out);

/* STATUS_REPORT (0xF0) payload: 56B normal / 72B extended by len */
int md_parse_status(const uint8_t* payload, uint16_t len, md_status_t* out);

/* DETECT_REPORT (0xF1) payload: [proto:1B][inv:1B][baud:4B LE] */
int md_parse_detect(const uint8_t* payload, uint16_t len, md_detect_t* out);

/* SBUS_DATA (0xF2) payload: [ch0~15:16x uint16 LE] */
int md_parse_sbus(const uint8_t* payload, uint16_t len, uint16_t ch[16]);

#if MD_ENABLE_CONFIG
/* config_t (231B raw) <-> md_config_t full-field parse/pack (bit fields
 * included), lossless round trip. Protected region (offset 0~10) is zeroed
 * on pack (firmware restores protected fields on write). */
int      md_parse_config(const uint8_t* raw, uint16_t len, md_config_t* out);
uint16_t md_pack_config(const md_config_t* cfg, uint8_t* out, uint16_t cap);
#endif

#ifdef __cplusplus
}
#endif

#endif /* MDC_LIB_H */
