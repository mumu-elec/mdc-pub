/**
 * @file    mdc_lib.h
 * @brief   Motor Driver Controller 通用调用库（STM32 HAL 平台）
 *
 * 协议依据：协议规范.md（布局 v2.1，config_t = 231B，18 条二进制命令）
 * API 规范：../mdc_lib/API.md（所有平台同一套 md_* 签名）
 *
 * 定位：纯打包 / 纯解析 —— 不 include 任何 HAL 头文件，不碰串口外设。
 *       用户拿到返回的字节自己调用 HAL_UART_Transmit() 发送；
 *       收到的字节喂给 md_parser_feed()（流式）或 md_parse_frame()（帧级）解析。
 *
 * 字节序：所有多字节字段小端序（LE）；多字节一律手动移位拼装，无结构体对齐依赖。
 *
 * 用法示例：
 *   uint8_t buf[64];
 *   uint16_t n = md_bin_motor_ctrl(100, 0, 0, 0, buf, sizeof(buf));
 *   HAL_UART_Transmit(&huart1, buf, n, 100);          // 串口由用户实现
 *
 *   // 接收中断里逐字节喂解析器：
 *   md_parser_feed(&g_parser, rx_byte, &cmd, &payload, &plen);
 */

#ifndef MDC_LIB_H
#define MDC_LIB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 常量（API.md §2.5，所有平台一致） ==================== */

#define MD_SYNC         0xAAu    /* 二进制帧同步字 */
#define MD_MAX_DATA     250u     /* DATA 段最大长度 */
#define MD_CONFIG_SIZE  231u     /* config_t 大小 */
#define MD_FRAME_MAX    235u     /* 最大整帧长度（4 + 231） */
#define MD_CRC8_POLY    0x07u    /* CRC8 多项式（初值 0，按位计算） */
#define MD_CMD_PING     0x01u    /* 连通性测试 */
#define MD_ERR_OK       0x00u    /* ACK 成功 */
#define MD_ERR_FAIL     0xFFu    /* ACK 失败 */

/* 二进制命令号（协议规范 §3.3） */
#define MD_CMD_READ_PARAM    0x10u   /* 读取全部配置（应答 config_t 231B） */
#define MD_CMD_WRITE_PARAM   0x11u   /* 写入全部配置（仅 RAM） */
#define MD_CMD_WRITE_FIELD   0x12u   /* 按偏移写入单个字段 */
#define MD_CMD_SAVE_EEPROM   0x20u   /* RAM → EEPROM */
#define MD_CMD_LOAD_EEPROM   0x21u   /* EEPROM → RAM */
#define MD_CMD_FACTORY_RESET 0x22u   /* 恢复出厂默认 */
#define MD_CMD_MOTOR_RAW     0x30u   /* 单通道 PWM 直驱 */
#define MD_CMD_MOTOR_CTRL    0x31u   /* 四通道批量控制（核心控制帧） */
#define MD_CMD_SUBSCRIBE     0x40u   /* 开启状态周期上报 */
#define MD_CMD_UNSUBSCRIBE   0x41u   /* 关闭状态上报 */
#define MD_CMD_DEBUG_SBUS    0x43u   /* SBUS 通道上报开关 */
#define MD_CMD_DEBUG_SPEED   0x44u   /* 速度原始值上报开关（0xF0 扩展为 72B） */
#define MD_CMD_ENTER_BL      0x52u   /* 软复位进 Bootloader */
#define MD_CMD_REBOOT        0x53u   /* 系统重启 */
#define MD_CMD_STATUS_REPORT 0xF0u   /* 状态上报（MCU 主动推送） */
#define MD_CMD_DETECT_REPORT 0xF1u   /* 协议检测结果上报 */
#define MD_CMD_SBUS_DATA     0xF2u   /* SBUS 16 通道原始值上报 */

/* 流式解析器缓冲大小（可裁剪；默认 256 可容纳最大帧 235B。
 * 51 等小内存平台可设小，但小于 235 时 READ_PARAM 的 231B 应答帧无法完整解析） */
#ifndef MD_PARSER_BUF
#define MD_PARSER_BUF 256u
#endif

/* config 全字段函数开关（51 等小内存平台置 0 可编译掉以省 RAM/代码，
 * 默认 1：启用 md_pack_config / md_parse_config / md_bin_write_param） */
#ifndef MD_ENABLE_CONFIG
#define MD_ENABLE_CONFIG 1
#endif

/* ==================== 数据结构 ==================== */

/* STATUS_REPORT（0xF0）解析结果，见 API.md §6.2
 * 56B 常规模式：enc/tgt/rpm + sbus_frame_cnt + sbus_ok_cnt
 * 72B 扩展模式：追加 rpm_raw[4]（需先 DEBUG_SPEED=1） */
typedef struct {
    int32_t  enc[4];          /* @0   编码器累计脉冲 */
    float    tgt[4];          /* @16  当前目标值 */
    int32_t  rpm[4];          /* @32  滤波后实时转速 */
    int32_t  rpm_raw[4];      /* @48  滤波前原始 RPM（72B 模式有效；56B 模式为 0） */
    uint32_t sbus_frame_cnt;  /* 56B:@48 / 72B:@64 */
    uint32_t sbus_ok_cnt;     /* 56B:@52 / 72B:@68 */
    uint8_t  extended;        /* 1=72B 扩展模式 */
} md_status_t;

/* DETECT_REPORT（0xF1）解析结果：proto=0 失败 1=SBUS 2=UART 3=ELRS */
typedef struct {
    uint8_t  proto;
    uint8_t  inv;
    uint32_t baud;            /* 小端 4B */
} md_detect_t;

/* ACK 解析结果：err=0x00 成功，任何非 0 视为失败 */
typedef struct {
    uint8_t cmd;              /* 完整 ACK 帧模式有效；仅传 DATA 段时为 0 */
    uint8_t err;
} md_ack_t;

/* config_t 全字段结构（API.md §6.5）。
 * 位域约定：control_mode/motor_invert 每电机 2bit；speed_pid_type/pos_pid_type/
 *           speed_filter_type 每电机 4bit；sbus_channel/rc_dir_ch 存储 1~16（解析 +1 / 打包 -1）；
 *           rc_map_mode 每电机 1bit，rc_dir_en 由同一字节 bit4-7 拆出。 */
typedef struct {
    /* 通讯 */
    uint32_t baud_rate;          /* @11 USART2 波特率 */
    uint16_t cmd_timeout_ms;     /* @15 超时保护（ms） */
    uint8_t  protocol;           /* @17 bit0-3: 1=SBUS 2=UART 3=ELRS */
    uint8_t  sbus_inv;           /* @17 bit4 */
    uint8_t  ctrl_priority;      /* @17 bit5: 0=USART2 优先 1=USB 优先 */
    /* 电机 ×4 */
    uint8_t  control_mode[4];    /* @18 每电机 2bit: 0开环 1速度 2位置 */
    uint8_t  motor_invert[4];    /* @19 每电机 2bit: bit0引脚反转 bit1编码器极性 */
    uint16_t encoder_cpr[4];     /* @20 编码器线数 */
    uint16_t speed_period_ms[4]; /* @28 速度环周期 */
    uint8_t  speed_pid_type[4];  /* @36 每电机 4bit: 0位置式 1增量式 */
    uint16_t speed_olim[4];      /* @38 速度环输出限幅（PWM 0~1000） */
    float    speed_kp[4];        /* @46+ 速度环 Kp */
    float    speed_ki[4];        /*      速度环 Ki */
    float    speed_kd[4];        /*      速度环 Kd */
    float    speed_ilim[4];      /*      速度环积分限幅 */
    uint16_t pos_period_ms[4];   /* @110 位置环周期 */
    uint8_t  pos_pid_type[4];    /* @118 每电机 4bit */
    float    pos_kp[4];          /* @120+ 位置环 Kp */
    float    pos_ki[4];          /*      位置环 Ki */
    float    pos_kd[4];          /*      位置环 Kd */
    float    pos_ilim[4];        /*      位置环积分限幅 */
    float    pos_olim[4];        /* @184 位置环输出限幅（RPM） */
    uint16_t pos_angle_cpr[4];   /* @200 位置环转一圈脉冲数（0=用 encoder_cpr） */
    uint8_t  speed_filter_type[4];   /* @208 每电机 4bit: 0无 1滑动平均 2低通 3中值 */
    uint8_t  speed_filter_window[4]; /* @210 滤波窗口 */
    uint8_t  sbus_channel[4];    /* @214 遥控通道映射（1~16，打包时 -1 存 0~15） */
    uint8_t  rc_dir_ch[4];       /* @216 方向映射通道（1~16） */
    uint8_t  rc_map_mode[4];     /* @218 bit0-3 每电机 1bit: 0中心零点 1min零点 */
    uint8_t  rc_dir_en[4];       /* @218 bit4-7 每电机 1bit */
    uint16_t sbus_param[4];      /* @219 遥控行程 */
    uint16_t sbus_range_min;     /* @227 通道值下边界 */
    uint16_t sbus_range_max;     /* @229 通道值上边界 */
} md_config_t;

/* 流式解析器状态（API.md §3.4）。
 * 缓冲由用户提供：小内存平台请声明在 xdata/静态区，例如 51 上：
 *   xdata md_parser_t g_parser; */
typedef struct {
    uint8_t  buf[MD_PARSER_BUF];
    uint16_t len;
} md_parser_t;

/* ==================== 底层：CRC / 组帧 / 帧解析 ==================== */

/* CRC8-ATM：多项式 0x07，初值 0，按位计算。
 * 校验向量：crc8({0x01,0x00})==0x15；crc8("123456789")==0xF4 */
uint8_t md_crc8(const uint8_t* data, uint16_t len);

/* 组帧：[0xAA][CMD][LEN][DATA...][CRC8]，CRC 范围 = CMD+LEN+DATA（不含 SYNC）。
 * 返回写入字节数；data_len>MD_MAX_DATA 或 cap 不足返回 0。
 * 验证向量：md_build_frame(0x01, NULL, 0) == {AA 01 00 15} */
uint16_t md_build_frame(uint8_t cmd, const uint8_t* data, uint16_t data_len,
                        uint8_t* out, uint16_t cap);

/* 帧级解析：校验 SYNC 与 CRC。成功返回 1 并输出 cmd/payload/payload_len；
 * payload 指向 frame 内部 DATA 起始，须在缓冲失效前消费。失败返回 0 */
int md_parse_frame(const uint8_t* frame, uint16_t len,
                   uint8_t* cmd, const uint8_t** payload, uint16_t* payload_len);

/* ==================== 流式解析器 ==================== */

void md_parser_init(md_parser_t* p);

/* 喂一个字节。收到完整且 CRC 通过的一帧时返回 1 并输出 cmd/payload/payload_len，
 * 否则返回 0。滑动窗口自动找 0xAA 同步；LEN>250 或 CRC 失败丢弃重扫。
 * 注意：payload 指向 p->buf 内部，在下次 feed 之前必须消费（复制出来）。
 * 文本回显行会被当作噪声丢弃（0xAA 前的字节直接忽略）。 */
int md_parser_feed(md_parser_t* p, uint8_t byte,
                   uint8_t* cmd, const uint8_t** payload, uint16_t* payload_len);

/* ==================== 文本指令层 ==================== */

/* 通用构造：cmd="/mode", args="1 speed" -> "/mode 1 speed\n"；
 * args=NULL -> "/mode\n"。写入含 '\n' 与 NUL，返回不含 NUL 的字节数；cap 不足返回 0 */
uint16_t md_text_build(const char* cmd, const char* args, char* out, uint16_t cap);

uint16_t md_text_version(char* out, uint16_t cap);   /* /version\n */
uint16_t md_text_help(char* out, uint16_t cap);      /* /help\n */
uint16_t md_text_status(char* out, uint16_t cap);    /* /status\n */
uint16_t md_text_check(char* out, uint16_t cap);     /* /check\n */
uint16_t md_text_detect(char* out, uint16_t cap);    /* /detect\n */
uint16_t md_text_save(char* out, uint16_t cap);      /* /save\n */
uint16_t md_text_load(char* out, uint16_t cap);      /* /load\n */
uint16_t md_text_reset(char* out, uint16_t cap);     /* /reset\n */
uint16_t md_text_enczero(uint8_t ch, char* out, uint16_t cap);  /* /enczero 1\n（ch 须 1~4） */
uint16_t md_text_mode(uint8_t ch, const char* mode, char* out, uint16_t cap);
/* mode=NULL -> "/mode 1\n"；mode="speed" -> "/mode 1 speed\n"（ch 须 1~4） */

/* 其余指令（speedctrl/posctrl/cpr/inv/einv/posangle/filter/uart2/priority/timeout/
 * smap/rmap/dmap/sbusparam/sbusrange）统一用 md_text_build 构造：
 *   md_text_build("/speedctrl", "1 0.5 0.02 0.01 500 800 10 0", out, cap);
 *   md_text_build("/uart2",     "115200 0 uart", out, cap);
 *   md_text_build("/sbusrange", "172 1811", out, cap); */

/* ==================== 二进制命令层（15 个打包函数） ==================== */

uint16_t md_bin_ping(uint8_t* out, uint16_t cap);            /* 0x01 */
uint16_t md_bin_read_param(uint8_t* out, uint16_t cap);      /* 0x10 */
uint16_t md_bin_write_param(const md_config_t* cfg, uint8_t* out, uint16_t cap); /* 0x11 */
uint16_t md_bin_write_field(uint16_t field_id, const uint8_t* value,
                            uint16_t value_len, uint8_t* out, uint16_t cap);     /* 0x12 */
uint16_t md_bin_save(uint8_t* out, uint16_t cap);            /* 0x20 */
uint16_t md_bin_load(uint8_t* out, uint16_t cap);            /* 0x21 */
uint16_t md_bin_factory_reset(uint8_t* out, uint16_t cap);   /* 0x22 */
uint16_t md_bin_motor_raw(uint8_t ch, uint8_t dir, uint16_t pwm,
                          uint8_t* out, uint16_t cap);       /* 0x30 ch=0~3 dir=0正/1反 pwm=0~1000 */
uint16_t md_bin_motor_ctrl(int32_t t0, int32_t t1, int32_t t2, int32_t t3,
                           uint8_t* out, uint16_t cap);      /* 0x31 4×int32 LE */
uint16_t md_bin_subscribe(uint16_t interval_ms, uint8_t* out, uint16_t cap); /* 0x40 固件钳位≥20ms */
uint16_t md_bin_unsubscribe(uint8_t* out, uint16_t cap);     /* 0x41 */
uint16_t md_bin_debug_sbus(uint8_t enable, uint8_t* out, uint16_t cap);       /* 0x43 */
uint16_t md_bin_debug_speed(uint8_t enable, uint8_t* out, uint16_t cap);      /* 0x44 */
uint16_t md_bin_enter_bl(uint8_t* out, uint16_t cap);        /* 0x52 */
uint16_t md_bin_reboot(uint8_t* out, uint16_t cap);          /* 0x53 */

/* ==================== 解析层 ==================== */

/* 解析 ACK：输入可为 ACK 帧 DATA 段（1 字节 err），也可为完整 ACK 帧
 * （AA CMD 01 err CRC，自动识别并校验）。成功返回 1，失败返回 0 */
int md_parse_ack(const uint8_t* payload, uint16_t len, md_ack_t* out);

/* STATUS_REPORT（0xF0）payload 解析：56B 常规 / 72B 扩展按 len 自动兼容 */
int md_parse_status(const uint8_t* payload, uint16_t len, md_status_t* out);

/* DETECT_REPORT（0xF1）payload 解析：[proto:1B][inv:1B][baud:4B LE] */
int md_parse_detect(const uint8_t* payload, uint16_t len, md_detect_t* out);

/* SBUS_DATA（0xF2）payload 解析：[ch0~15:16×uint16 LE] */
int md_parse_sbus(const uint8_t* payload, uint16_t len, uint16_t ch[16]);

#if MD_ENABLE_CONFIG
/* config_t（231B raw）↔ md_config_t 全字段解析/打包（含位域），往返无损。
 * 受保护区（offset 0~10）打包时置 0（固件写入时自动还原） */
int      md_parse_config(const uint8_t* raw, uint16_t len, md_config_t* out);
uint16_t md_pack_config(const md_config_t* cfg, uint8_t* out, uint16_t cap);
#endif

#ifdef __cplusplus
}
#endif

#endif /* MDC_LIB_H */
