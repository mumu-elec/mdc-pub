/*
 * ============================================================================
 *  Motor Driver Controller — Arduino UNO 例程（基于 mdc_lib 通用调用库）
 * ============================================================================
 *  硬件：Arduino UNO（ATmega328P，16MHz）
 *  连接：SoftwareSerial(10, 11) 接控制板 RC 接口（USART2）
 *        - UNO TX = 11  → 控制板 RC 信号（RC RX）
 *        - UNO RX = 10  ← 控制板 RC 信号（RC TX）
 *        - GND 共地
 *  波特率：115200（使用前请先在控制板上执行 /uart2 115200 0 uart）
 *
 *  协议层全部由 mdc_lib 完成（打包 / 解析），本文件只负责串口收发：
 *    - 发送：md_text_* / md_text_build 构造文本指令，md_bin_* 构造二进制帧，
 *            把库返回的字节交给 rcSerial.write() 发送（CRC8/组帧都在库内）
 *    - 接收：RC 口字节逐字节喂 md_parser_feed()（自动找 0xAA 同步 + CRC 校验），
 *            收到 0xF0 状态帧用 md_parse_status() 解析并打印到 USB 串口
 *
 *  功能：
 *    1. USB 串口（串口监视器）交互终端：输入任意文本指令 → 转发到 RC 口
 *    2. 数字快捷键菜单：1=/version 2=/status 3=开环正转(PWM 400) 4=开环反转(-400)
 *       5=停 6=速度模式目标100RPM(/enczero + /mode 1 speed + 0x31) 7=/save
 *    3. 上电用 md_bin_subscribe 订阅状态上报，接收并解析 0xF0 状态帧
 *    4. 快捷键 3/4/6 开启后按 50ms 周期连续发送 0x31 控制帧
 *
 *  依赖：mdc_lib.h / mdc_lib.cpp（来源 mdc_lib/avr/arduino_uno/，复制到本
 *        sketch 目录，Arduino IDE 自动编译同目录 .cpp）
 * ============================================================================
 */

/* 说明：本例程使用 mdc_lib 默认配置（MD_PARSER_BUF=256，解析器全局静态，
 * UNO 2KB SRAM 可容纳）。如需裁剪 MD_PARSER_BUF，必须通过编译选项
 * （-DMD_PARSER_BUF=xxx）同时作用于 mdc_lib.cpp，否则 .ino 与 .cpp 的
 * 结构体布局不一致会导致解析器越界，不建议初学者修改。 */

#include "mdc_lib.h"
#include <SoftwareSerial.h>
#include <string.h>

/* ---- 引脚定义：SoftwareSerial(rxPin, txPin) ---- */
#define RC_RX_PIN  10   /* UNO RX ← 控制板 RC TX */
#define RC_TX_PIN  11   /* UNO TX → 控制板 RC RX */

SoftwareSerial rcSerial(RC_RX_PIN, RC_TX_PIN);

/* ---- 控制参数 ---- */
#define CTRL_SEND_MS    50     /* 连续控制帧发送间隔（规范建议 30/50/100ms） */
#define SUBSCRIBE_MS    100    /* 状态上报间隔（固件钳位 ≥20ms） */

static int32_t  g_target[4] = {0, 0, 0, 0};   /* 当前目标值（int32，mdc_lib 负责小端打包） */
static bool     g_cont      = false;          /* 是否持续发送控制帧 */
static uint32_t g_lastSend  = 0;

/* 流式解析器状态：全局/static，避免占栈（UNO SRAM 仅 2KB） */
static md_parser_t g_parser;

/* ---- 缓冲 ---- */
static uint8_t g_tx[32];            /* 二进制帧打包缓冲（0x31 帧 20B、0x40 帧 6B） */
static char    g_text[160];         /* 文本指令构造缓冲（md_text_* 写入） */
static char    g_usbLine[128];      /* USB 输入行缓冲 */
static uint8_t g_usbLineLen = 0;
static char    g_rxLine[128];       /* RC 口文本应答行缓冲（控制板文本应答以 '\n' 结尾） */
static uint8_t g_rxLineLen = 0;

/* ============================================================================
 * 发送辅助：把 mdc_lib 打包结果交给串口（用户侧收发）
 * ==========================================================================*/
static void tx_bin(const uint8_t *buf, uint16_t n)
{
    if (n > 0) {
        rcSerial.write(buf, n);
    }
}

static void tx_text_built(uint16_t n)
{
    if (n > 0) {
        rcSerial.write((const uint8_t *)g_text, n);   /* g_text 含 '\n' 结尾 */
    }
}

/* 发送 0x31 MOTOR_CTRL 控制帧（mdc_lib 打包整帧，含同步字 + CRC8）。
 * 目标值含义取决于各通道模式：开环 = PWM(±1000)，速度 = RPM，位置 = 0.1° */
static void send_motor_ctrl(void)
{
    uint16_t n = md_bin_motor_ctrl(g_target[0], g_target[1], g_target[2], g_target[3],
                                   g_tx, sizeof(g_tx));
    tx_bin(g_tx, n);
}

/* ---- USB 串口打印帮助菜单 ---- */
void printMenu(void)
{
    Serial.println(F("=== Motor Driver Controller - Arduino UNO (mdc_lib) ==="));
    Serial.println(F("输入文本指令（如 /version）自动转发到 RC 口"));
    Serial.println(F("数字快捷键："));
    Serial.println(F("  1 = /version"));
    Serial.println(F("  2 = /status"));
    Serial.println(F("  3 = 开环正转 PWM=400 (ch1, 持续)"));
    Serial.println(F("  4 = 开环反转 PWM=-400 (ch1, 持续)"));
    Serial.println(F("  5 = 停止 (ch1~4, 结束持续发送)"));
    Serial.println(F("  6 = 速度模式目标 100RPM (/enczero 1 + /mode 1 speed + 0x31)"));
    Serial.println(F("  7 = /save"));
    Serial.println(F("-----------------------------------------------"));
}

/* ---- 快捷键菜单处理（文本指令与二进制帧均用 mdc_lib 构造） ---- */
void runMenu(uint8_t n)
{
    uint16_t len;

    switch (n) {
    case 1:
        Serial.println(F(">>> /version"));
        len = md_text_version(g_text, sizeof(g_text));
        tx_text_built(len);
        break;
    case 2:
        Serial.println(F(">>> /status"));
        len = md_text_status(g_text, sizeof(g_text));
        tx_text_built(len);
        break;
    case 3:
        Serial.println(F(">>> 开环正转 PWM=400 (ch1)"));
        g_target[0] = 400; g_target[1] = 0; g_target[2] = 0; g_target[3] = 0;
        g_cont = true;
        send_motor_ctrl();
        break;
    case 4:
        Serial.println(F(">>> 开环反转 PWM=-400 (ch1)"));
        g_target[0] = -400; g_target[1] = 0; g_target[2] = 0; g_target[3] = 0;
        g_cont = true;
        send_motor_ctrl();
        break;
    case 5:
        Serial.println(F(">>> 停止 (ch1~4)"));
        g_cont = false;
        g_target[0] = 0; g_target[1] = 0; g_target[2] = 0; g_target[3] = 0;
        send_motor_ctrl();                       /* 立即发一帧清零 */
        break;
    case 6:
        Serial.println(F(">>> /enczero 1 + /mode 1 speed + 目标 100RPM (ch1)"));
        len = md_text_enczero(1, g_text, sizeof(g_text));   /* 清零 ch1 编码器 */
        tx_text_built(len);
        delay(20);
        len = md_text_mode(1, "speed", g_text, sizeof(g_text));  /* ch1 切速度闭环 */
        tx_text_built(len);
        delay(20);
        g_target[0] = 100; g_target[1] = 0; g_target[2] = 0; g_target[3] = 0;
        g_cont = true;
        send_motor_ctrl();
        break;
    case 7:
        Serial.println(F(">>> /save"));
        len = md_text_save(g_text, sizeof(g_text));
        tx_text_built(len);
        break;
    default:
        break;
    }
}

/* ---- 处理一行 USB 输入：单数字=快捷键，否则按文本指令转发 ---- */
void handleLine(char *line)
{
    uint16_t len;
    uint8_t  lineLen = (uint8_t)strlen(line);

    /* 单个数字 1~7 → 快捷键菜单 */
    if (lineLen == 1 && line[0] >= '1' && line[0] <= '7') {
        runMenu((uint8_t)(line[0] - '0'));
        return;
    }
    /* 其它 → 文本指令：md_text_build 补 '\n' 后转发到 RC 口 */
    Serial.print(F(">>> "));
    Serial.println(line);
    len = md_text_build(line, NULL, g_text, sizeof(g_text));
    tx_text_built(len);
}

void setup()
{
    Serial.begin(115200);          /* USB 串口监视器 */
    rcSerial.begin(115200);        /* RC 口（控制板需先 /uart2 115200 0 uart） */

    md_parser_init(&g_parser);

    while (!Serial) { ; }          /* Leonardo 等原生 USB 板需要等待；UNO 直接跳过 */
    printMenu();

    /* 订阅状态上报（100ms）：控制板回 ACK 后按周期推送 0xF0，
     * 由 loop() 里的 md_parser_feed 解析 */
    uint16_t n = md_bin_subscribe(SUBSCRIBE_MS, g_tx, sizeof(g_tx));
    tx_bin(g_tx, n);
}

void loop()
{
    /* 1) USB → RC：逐字节收行，遇换行处理 */
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (g_usbLineLen > 0) {
                g_usbLine[g_usbLineLen] = '\0';
                handleLine(g_usbLine);
                g_usbLineLen = 0;
            }
        } else if (g_usbLineLen < sizeof(g_usbLine) - 1) {
            g_usbLine[g_usbLineLen++] = c;
        }
    }

    /* 2) RC → USB：
     *    二进制帧逐字节喂 md_parser_feed（0xF0 解析打印，ACK 打印 err）；
     *    文本应答按可打印字符收行，遇换行回显到 USB（二进制帧字节被过滤） */
    while (rcSerial.available()) {
        uint8_t b = (uint8_t)rcSerial.read();
        uint8_t cmd;
        const uint8_t *payload;
        uint16_t plen;

        /* 二进制帧解析（mdc_lib：滑动窗口找 0xAA + CRC 校验） */
        if (md_parser_feed(&g_parser, b, &cmd, &payload, &plen)) {
            if (cmd == MD_CMD_STATUS_REPORT) {           /* 0xF0 状态上报 */
                md_status_t st;
                if (md_parse_status(payload, plen, &st)) {
                    /* payload 指向解析器内部缓冲，解析完成即消费 */
                    Serial.print(F("[0xF0] enc="));
                    Serial.print(st.enc[0]); Serial.print(',');
                    Serial.print(st.enc[1]); Serial.print(',');
                    Serial.print(st.enc[2]); Serial.print(',');
                    Serial.print(st.enc[3]);
                    Serial.print(F(" tgt="));
                    Serial.print(st.tgt[0], 1); Serial.print(',');
                    Serial.print(st.tgt[1], 1); Serial.print(',');
                    Serial.print(st.tgt[2], 1); Serial.print(',');
                    Serial.print(st.tgt[3], 1);
                    Serial.print(F(" rpm="));
                    Serial.print(st.rpm[0]); Serial.print(',');
                    Serial.print(st.rpm[1]); Serial.print(',');
                    Serial.print(st.rpm[2]); Serial.print(',');
                    Serial.println(st.rpm[3]);
                }
            } else if (plen == 1) {                      /* ACK 帧（如 0x40 应答） */
                md_ack_t ack;
                if (md_parse_ack(payload, plen, &ack)) {
                    Serial.print(F("[ACK] cmd=0x"));
                    Serial.print(ack.cmd, HEX);
                    Serial.print(F(" err=0x"));
                    Serial.println(ack.err, HEX);
                }
            }
            /* 其它上报帧（0xF1/0xF2 等）按需扩展 */
            g_rxLineLen = 0;                             /* 完整帧已消费，丢弃行缓冲残留 */
        }

        /* 文本应答收行（仅可打印 ASCII，二进制帧字节不进入行缓冲） */
        if (b == '\n' || b == '\r') {
            if (g_rxLineLen > 0) {
                g_rxLine[g_rxLineLen] = '\0';
                Serial.print(F("<< "));
                Serial.println(g_rxLine);
                g_rxLineLen = 0;
            }
        } else if (b >= 0x20 && b <= 0x7E && g_rxLineLen < sizeof(g_rxLine) - 1) {
            g_rxLine[g_rxLineLen++] = (char)b;
        }
    }

    /* 3) 持续控制：菜单 3/4/6 开启后按 50ms 周期连续发送 0x31（规范 §3.3 实时控制要求） */
    if (g_cont && (millis() - g_lastSend >= CTRL_SEND_MS)) {
        send_motor_ctrl();
        g_lastSend = millis();
    }
}
