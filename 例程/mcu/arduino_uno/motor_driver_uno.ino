/*
 * ============================================================================
 *  Motor Driver Controller — Arduino UNO 上位机例程
 * ============================================================================
 *  硬件：Arduino UNO（ATmega328P，16MHz）
 *  连接：SoftwareSerial(10, 11) 接控制板 RC 接口（USART2）
 *        - UNO TX = 11  → 控制板 RC 信号（RC RX）
 *        - UNO RX = 10  ← 控制板 RC 信号（RC TX）
 *        - GND 共地
 *  波特率：115200（使用前请先在控制板上执行 /uart2 115200 0 uart）
 *
 *  功能：
 *    1. USB 串口（串口监视器）交互终端：输入任意文本指令 → 转发到 RC 口并回显
 *    2. 数字快捷键菜单：1=/version 2=/status 3=开环正转(PWM 400) 4=开环反转(-400)
 *       5=停 6=速度模式目标100RPM(/mode 1 speed + 0x31) 7=/save
 *    3. 二进制帧封装：crc8() / send_frame() / send_motor_ctrl()（0x31 控制帧）
 *
 *  协议依据：common/协议规范.md §2（文本指令）、§3（二进制帧）
 *  控制帧 0x31 走 RC 口（USART2），默认控制优先级 USART2 优先，天然生效。
 * ============================================================================
 */

#include <SoftwareSerial.h>

/* ---- 引脚定义：SoftwareSerial(rxPin, txPin) ---- */
#define RC_RX_PIN  10   /* UNO RX ← 控制板 RC TX */
#define RC_TX_PIN  11   /* UNO TX → 控制板 RC RX */

SoftwareSerial rcSerial(RC_RX_PIN, RC_TX_PIN);

/* ---- 协议常量（见协议规范 §3.3） ---- */
#define FRAME_SYNC      0xAA
#define CMD_MOTOR_CTRL  0x31   /* MOTOR_CTRL：四通道批量控制帧 */

/* ---- 控制参数 ---- */
#define CTRL_SEND_MS    50     /* 连续控制帧发送间隔（规范建议 30/50/100ms） */

static int32_t  g_target[4] = {0, 0, 0, 0};   /* 当前目标值（int32，小端） */
static bool     g_cont      = false;          /* 是否持续发送控制帧 */
static uint32_t g_lastSend  = 0;

/* ============================================================================
 * CRC8：多项式 0x07，初值 0，计算范围 = CMD + LEN + DATA（不含 SYNC）
 * 严格按协议规范 §3.1 参考实现移植
 * ==========================================================================*/
uint8_t crc8(const uint8_t *d, uint16_t len)
{
    uint8_t c = 0;
    for (uint16_t i = 0; i < len; i++) {
        c ^= d[i];
        for (uint8_t b = 0; b < 8; b++)
            c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
    }
    return c;
}

/* ============================================================================
 * 组帧并发送：[0xAA][CMD][LEN][DATA...][CRC8]
 * CRC8 计算范围 = CMD + LEN + DATA
 * ==========================================================================*/
void send_frame(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    uint8_t buf[256];
    uint16_t idx = 0;

    buf[idx++] = FRAME_SYNC;
    buf[idx++] = cmd;
    buf[idx++] = len;
    for (uint8_t i = 0; i < len; i++) {
        buf[idx++] = data[i];
    }
    buf[idx] = crc8(buf + 1, idx - 1);   /* idx-1 = 2 + len 字节（CMD+LEN+DATA） */
    rcSerial.write(buf, idx + 1);
}

/* int32 小端手动拼装（逐字节移位，避免依赖 union 对齐） */
static void put_i32_le(uint8_t *buf, uint16_t *idx, int32_t v)
{
    buf[(*idx)++] = (uint8_t)(v & 0xFF);
    buf[(*idx)++] = (uint8_t)((v >> 8) & 0xFF);
    buf[(*idx)++] = (uint8_t)((v >> 16) & 0xFF);
    buf[(*idx)++] = (uint8_t)((v >> 24) & 0xFF);
}

/* ============================================================================
 * 发送 0x31 MOTOR_CTRL 控制帧
 * DATA = [m1~m4: 4×int32 LE]；含义取决于各通道模式：
 *        开环 = PWM(±1000)，速度 = RPM，位置 = 0.1°(±3600)
 * ==========================================================================*/
void send_motor_ctrl(const int32_t targets[4])
{
    uint8_t data[16];
    uint16_t idx = 0;

    for (int i = 0; i < 4; i++) {
        put_i32_le(data, &idx, targets[i]);
    }
    send_frame(CMD_MOTOR_CTRL, data, 16);
}

/* 发送文本指令（以换行结尾，规范 §2.1） */
void send_text(const char *cmd)
{
    rcSerial.print(cmd);
    rcSerial.print('\n');
}

/* ---- USB 串口打印帮助菜单 ---- */
void printMenu(void)
{
    Serial.println(F("=== Motor Driver Controller - Arduino UNO ==="));
    Serial.println(F("输入文本指令（如 /version）自动转发到 RC 口"));
    Serial.println(F("数字快捷键："));
    Serial.println(F("  1 = /version"));
    Serial.println(F("  2 = /status"));
    Serial.println(F("  3 = 开环正转 PWM=400 (ch1, 持续)"));
    Serial.println(F("  4 = 开环反转 PWM=-400 (ch1, 持续)"));
    Serial.println(F("  5 = 停止 (ch1~4, 结束持续发送)"));
    Serial.println(F("  6 = 速度模式目标 100RPM (ch1, /mode 1 speed + 0x31)"));
    Serial.println(F("  7 = /save"));
    Serial.println(F("-----------------------------------------------"));
}

/* ---- 快捷键菜单处理 ---- */
void runMenu(uint8_t n)
{
    switch (n) {
    case 1:
        Serial.println(F(">>> /version"));
        send_text("/version");
        break;
    case 2:
        Serial.println(F(">>> /status"));
        send_text("/status");
        break;
    case 3:
        Serial.println(F(">>> 开环正转 PWM=400 (ch1)"));
        g_target[0] = 400; g_target[1] = 0; g_target[2] = 0; g_target[3] = 0;
        g_cont = true;
        send_motor_ctrl(g_target);
        break;
    case 4:
        Serial.println(F(">>> 开环反转 PWM=-400 (ch1)"));
        g_target[0] = -400; g_target[1] = 0; g_target[2] = 0; g_target[3] = 0;
        g_cont = true;
        send_motor_ctrl(g_target);
        break;
    case 5:
        Serial.println(F(">>> 停止 (ch1~4)"));
        g_cont = false;
        g_target[0] = 0; g_target[1] = 0; g_target[2] = 0; g_target[3] = 0;
        send_motor_ctrl(g_target);   /* 立即发一帧清零 */
        break;
    case 6:
        Serial.println(F(">>> /mode 1 speed + 目标 100RPM (ch1)"));
        send_text("/mode 1 speed");           /* 先把 ch1 切到速度闭环 */
        delay(20);                            /* 留出处理时间 */
        g_target[0] = 100; g_target[1] = 0; g_target[2] = 0; g_target[3] = 0;
        g_cont = true;
        send_motor_ctrl(g_target);
        break;
    case 7:
        Serial.println(F(">>> /save"));
        send_text("/save");
        break;
    default:
        break;
    }
}

/* ---- 处理一行 USB 输入：单数字=快捷键，否则按文本指令转发 ---- */
static char g_line[128];
static uint8_t g_lineLen = 0;

void handleLine(char *line)
{
    uint8_t len = strlen(line);

    /* 单个数字 1~7 → 快捷键菜单 */
    if (len == 1 && line[0] >= '1' && line[0] <= '7') {
        runMenu((uint8_t)(line[0] - '0'));
        return;
    }
    /* 其它 → 文本指令转发到 RC 口 */
    Serial.print(F(">>> "));
    Serial.println(line);
    send_text(line);
}

void setup()
{
    Serial.begin(115200);          /* USB 串口监视器 */
    rcSerial.begin(115200);        /* RC 口（控制板需先 /uart2 115200 0 uart） */

    while (!Serial) { ; }          /* Leonardo 等原生 USB 板需要等待；UNO 直接跳过 */
    printMenu();
}

void loop()
{
    /* 1) USB → RC：逐字节收行，遇换行处理 */
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (g_lineLen > 0) {
                g_line[g_lineLen] = '\0';
                handleLine(g_line);
                g_lineLen = 0;
            }
        } else if (g_lineLen < sizeof(g_line) - 1) {
            g_line[g_lineLen++] = c;
        }
    }

    /* 2) RC → USB：控制板回显 / 上报，原样打印 */
    while (rcSerial.available()) {
        Serial.write(rcSerial.read());
    }

    /* 3) 持续控制：菜单 3/4/6 开启后按 50ms 周期连续发送 0x31（规范 §3.3 实时控制要求） */
    if (g_cont && (millis() - g_lastSend >= CTRL_SEND_MS)) {
        send_motor_ctrl(g_target);
        g_lastSend = millis();
    }
}
