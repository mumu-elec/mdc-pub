/*
 * ============================================================================
 *  Motor Driver Controller — ESP32 Arduino 上位机例程
 * ============================================================================
 *  硬件：ESP32 DevKit（如 ESP32-WROOM-32）
 *  连接：HardwareSerial Serial2(17, 16) 接控制板 RC 接口（USART2）
 *        - ESP32 TX = 17 → 控制板 RC 信号（RC RX）
 *        - ESP32 RX = 16 ← 控制板 RC 信号（RC TX）
 *        - GND 共地
 *  波特率：115200（使用前请先在控制板上执行 /uart2 115200 0 uart）
 *
 *  两个 FreeRTOS 任务：
 *    - task_recv：读取 Serial2 回显/上报，解析 0xF0 状态帧打印到 USB Serial
 *    - task_send：每 50ms 发 0x31 控制帧；目标值通过 USB Serial 命令修改
 *
 *  USB Serial 命令（串口监视器输入，回车执行）：
 *    pwm 400         → 开环目标 PWM=400（ch1，开环模式下生效）
 *    speed 200       → 速度目标 200RPM（ch1，速度闭环模式下生效）
 *    stop            → 四通道目标清零
 *    mode 1 speed    → 等价于文本指令 /mode 1 speed（转发到 RC 口）
 *    /version 等     → 任意以 / 开头的文本指令原样转发到 RC 口
 *
 *  协议依据：common/协议规范.md §2（文本指令）、§3（二进制帧）
 *  默认控制优先级 USART2 优先 → ESP32（USART2 侧）控制帧天然生效。
 * ============================================================================
 */

/* ---- RC 口 UART（HardwareSerial( rxPin, txPin )）---- */
#define RC_RX_PIN  16    /* ESP32 RX ← 控制板 RC TX */
#define RC_TX_PIN  17    /* ESP32 TX → 控制板 RC RX */
HardwareSerial rcSerial(2);                    /* UART2 外设 */

/* ---- 协议常量（规范 §3.3） ---- */
#define FRAME_SYNC        0xAA
#define CMD_MOTOR_CTRL    0x31    /* 四通道批量控制帧 */
#define CMD_SUBSCRIBE     0x40    /* 状态周期上报开关 */
#define CMD_STATUS_REPORT 0xF0    /* 周期状态上报（MCU 主动推送） */

#define CTRL_SEND_MS      50      /* 控制帧发送周期（规范建议 30/50/100ms） */
#define SUBSCRIBE_MS      100     /* 状态上报间隔 */

/* ---- 全局控制目标（task_send 使用，USB 命令线程修改） ---- */
static int32_t g_target[4] = {0, 0, 0, 0};

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

/* 组帧并发送：[0xAA][CMD][LEN][DATA...][CRC8] */
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
    buf[idx] = crc8(buf + 1, idx - 1);   /* CRC 范围 = CMD+LEN+DATA */
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

/* 发送 0x31 MOTOR_CTRL：DATA = [m1~m4: 4×int32 LE]（16B） */
void send_motor_ctrl(const int32_t targets[4])
{
    uint8_t data[16];
    uint16_t idx = 0;

    for (int i = 0; i < 4; i++) {
        put_i32_le(data, &idx, targets[i]);
    }
    send_frame(CMD_MOTOR_CTRL, data, 16);
}

/* 发送文本指令（以 '\n' 结尾，规范 §2.1） */
void send_text(const String &cmd)
{
    rcSerial.print(cmd);
    rcSerial.print('\n');
}

/* 发送 0x40 SUBSCRIBE：[interval_ms:2B LE] */
void subscribe(uint16_t interval_ms)
{
    uint8_t data[2];
    data[0] = (uint8_t)(interval_ms & 0xFF);
    data[1] = (uint8_t)((interval_ms >> 8) & 0xFF);
    send_frame(CMD_SUBSCRIBE, data, 2);
}

/* ============================================================================
 * 接收解析：滑动窗口状态机（与 STM32 例程相同的帧结构）
 * ==========================================================================*/
enum { RX_WAIT_SYNC, RX_CMD, RX_LEN, RX_DATA, RX_CRC };
static uint8_t  s_rxState = RX_WAIT_SYNC;
static uint8_t  s_rxBuf[256];      /* [0]=CMD [1]=LEN [2..]=DATA */
static uint8_t  s_rxLen;
static uint16_t s_rxIdx;

/* 0xF0 解析（规范 §4，常规模式 56B）：
 *   [enc1~4:4×int32 LE][tgt1~4:4×float LE][rpm1~4:4×int32 LE]
 *   [sbus_frame_cnt:4B LE][sbus_ok_cnt:4B LE]
 * ESP32 为小端机，float 直接 memcpy 位模式即可。 */
static int32_t get_i32_le(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static void handle_status_report(const uint8_t *data, uint8_t len)
{
    if (len >= 56) {
        int32_t enc[4], rpm[4];
        float   tgt[4];

        for (int i = 0; i < 4; i++) {
            enc[i] = get_i32_le(data + i * 4);
            memcpy(&tgt[i], data + 16 + i * 4, 4);   /* float LE 位模式 */
            rpm[i] = get_i32_le(data + 32 + i * 4);
        }
        Serial.printf("[0xF0] enc=%ld,%ld,%ld,%ld tgt=%.1f,%.1f,%.1f,%.1f rpm=%ld,%ld,%ld,%ld\r\n",
                      (long)enc[0], (long)enc[1], (long)enc[2], (long)enc[3],
                      (double)tgt[0], (double)tgt[1], (double)tgt[2], (double)tgt[3],
                      (long)rpm[0], (long)rpm[1], (long)rpm[2], (long)rpm[3]);
    }
}

/* 每收到一个 RC 口字节调用一次 */
static void rx_byte(uint8_t b)
{
    switch (s_rxState) {
    case RX_WAIT_SYNC:
        if (b == FRAME_SYNC) s_rxState = RX_CMD;
        break;
    case RX_CMD:
        s_rxBuf[0] = b;
        s_rxState = RX_LEN;
        break;
    case RX_LEN:
        s_rxBuf[1] = b;
        s_rxLen = b;
        s_rxIdx = 2;
        s_rxState = (s_rxLen == 0) ? RX_CRC : RX_DATA;
        break;
    case RX_DATA:
        if (s_rxIdx < sizeof(s_rxBuf)) s_rxBuf[s_rxIdx++] = b;
        if (s_rxIdx >= (uint16_t)(s_rxLen + 2)) s_rxState = RX_CRC;
        break;
    case RX_CRC: {
        uint8_t calc = crc8(s_rxBuf, (uint16_t)(s_rxLen + 2));  /* CMD+LEN+DATA */
        if (b == calc && s_rxBuf[0] == CMD_STATUS_REPORT) {
            handle_status_report(&s_rxBuf[2], s_rxLen);
        }
        s_rxState = (b == FRAME_SYNC) ? RX_CMD : RX_WAIT_SYNC;  /* 滑动窗口 */
        break;
    }
    default:
        s_rxState = RX_WAIT_SYNC;
        break;
    }
}

/* ============================================================================
 * task_recv：读 Serial2 回显/上报 → 帧解析 → 打印到 USB Serial
 * ==========================================================================*/
void task_recv(void *pv)
{
    (void)pv;
    while (1) {
        while (rcSerial.available()) {
            rx_byte((uint8_t)rcSerial.read());
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

/* ============================================================================
 * task_send：每 50ms 发一帧 0x31 控制帧（规范 §3.3 实时控制需连续发送）
 * ==========================================================================*/
void task_send(void *pv)
{
    (void)pv;
    TickType_t last = xTaskGetTickCount();

    while (1) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(CTRL_SEND_MS));
        send_motor_ctrl(g_target);
    }
}

/* ============================================================================
 * USB Serial 命令处理
 * ==========================================================================*/
static void handle_usb_line(String line)
{
    line.trim();
    if (line.length() == 0) return;

    /* 1) 任意文本指令：以 / 开头 → 原样转发 */
    if (line.startsWith("/")) {
        Serial.print(">>> "); Serial.println(line);
        send_text(line);
        return;
    }
    /* 2) mode 1 speed → 补 / 转发为文本指令 */
    if (line.startsWith("mode ")) {
        String cmd = "/" + line;
        Serial.print(">>> "); Serial.println(cmd);
        send_text(cmd);
        return;
    }
    /* 3) pwm <val> → 开环目标（ch1，需先 /mode 1 open） */
    if (line.startsWith("pwm ")) {
        int32_t v = (int32_t)line.substring(4).toInt();
        g_target[0] = v; g_target[1] = 0; g_target[2] = 0; g_target[3] = 0;
        Serial.printf(">>> 开环目标 PWM=%ld (ch1)\r\n", (long)v);
        return;
    }
    /* 4) speed <rpm> → 速度目标（ch1，需先 /mode 1 speed） */
    if (line.startsWith("speed ")) {
        int32_t v = (int32_t)line.substring(6).toInt();
        g_target[0] = v; g_target[1] = 0; g_target[2] = 0; g_target[3] = 0;
        Serial.printf(">>> 速度目标 %ldRPM (ch1)\r\n", (long)v);
        return;
    }
    /* 5) stop → 四通道清零 */
    if (line.equals("stop")) {
        g_target[0] = 0; g_target[1] = 0; g_target[2] = 0; g_target[3] = 0;
        Serial.println(">>> 停止 (ch1~4)");
        return;
    }
    Serial.printf("未知命令: %s （支持 pwm <v> / speed <rpm> / stop / mode <ch> <mode> / /xxx 文本指令）\r\n", line.c_str());
}

void setup()
{
    Serial.begin(115200);               /* USB Serial（串口监视器） */
    rcSerial.begin(115200, SERIAL_8N1, RC_RX_PIN, RC_TX_PIN);   /* RC 口 */

    delay(200);
    Serial.println("=== Motor Driver Controller - ESP32 ===");
    Serial.println("命令: pwm <v> | speed <rpm> | stop | mode <ch> <mode> | /文本指令");

    /* 订阅状态上报（100ms），task_recv 会解析 0xF0 打印 */
    subscribe(SUBSCRIBE_MS);

    /* 创建两个 FreeRTOS 任务 */
    xTaskCreate(task_recv, "task_recv", 4096, NULL, 1, NULL);
    xTaskCreate(task_send, "task_send", 4096, NULL, 1, NULL);

    /* 本线程继续负责 USB Serial 命令读取 */
}

void loop()
{
    /* USB Serial 逐行读命令（Arduino loop 运行在 core1，与两个任务并行） */
    static String line;

    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n') {
            handle_usb_line(line);
            line = "";
        } else if (c != '\r') {
            line += c;
        }
    }
    delay(5);
}
