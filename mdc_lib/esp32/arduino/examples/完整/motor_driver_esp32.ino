/*
 * ============================================================================
 *  Motor Driver Controller — ESP32 Arduino 例程（基于 mdc_lib 通用调用库）
 * ============================================================================
 *  硬件：ESP32 DevKit（如 ESP32-WROOM-32）
 *  连接：HardwareSerial Serial2(17, 16) 接控制板 RC 接口（USART2）
 *        - ESP32 TX = 17 → 控制板 RC 信号（RC RX）
 *        - ESP32 RX = 16 ← 控制板 RC 信号（RC TX）
 *        - GND 共地
 *  波特率：115200（使用前请先在控制板上执行 /uart2 115200 0 uart）
 *
 *  协议层全部由 mdc_lib 完成（打包 / 解析），本文件只负责串口收发：
 *    - 发送：md_bin_* / md_text_* 返回「要发送的字节」，交给 Serial2.write()
 *    - 接收：Serial2 字节逐字节喂 md_parser_feed()（自动找 0xAA 同步 + CRC 校验），
 *            收到 0xF0 状态帧用 md_parse_status() 解析并打印到 USB Serial
 *
 *  两个 FreeRTOS 任务：
 *    - task_recv：读 Serial2 → md_parser_feed 解析 0xF0 → 打印到 USB Serial
 *    - task_send：每 50ms 用 md_bin_motor_ctrl 打包 0x31 控制帧并发送
 *
 *  USB Serial 命令（串口监视器输入，回车执行）：
 *    pwm 400         → 开环目标 PWM=400（ch1，开环模式下生效）
 *    speed 200       → 速度目标 200RPM（ch1，速度闭环模式下生效）
 *    stop            → 四通道目标清零
 *    mode 1 speed    → 等价于文本指令 /mode 1 speed（md_text_mode 构造后转发）
 *    /version 等     → 任意以 / 开头的文本指令原样转发（md_text_build 构造）
 *
 *  依赖：mdc_lib.h / mdc_lib.cpp（来源 mdc_lib/esp32/arduino/，复制到本
 *        sketch 目录，Arduino IDE 自动编译同目录 .cpp）
 * ============================================================================
 */

#include "mdc_lib.h"
#include <HardwareSerial.h>

/* ---- RC 口 UART（HardwareSerial( rxPin, txPin )）---- */
#define RC_RX_PIN  16    /* ESP32 RX ← 控制板 RC TX */
#define RC_TX_PIN  17    /* ESP32 TX → 控制板 RC RX */
HardwareSerial rcSerial(2);                    /* UART2 外设 */

#define CTRL_SEND_MS      50      /* 控制帧发送周期（规范建议 30/50/100ms） */
#define SUBSCRIBE_MS      100     /* 状态上报间隔（固件钳位 ≥20ms） */

/* ---- 全局控制目标（task_send 使用，USB 命令线程修改） ---- */
static int32_t g_target[4] = {0, 0, 0, 0};

/* 流式解析器状态（全局，避免占栈） */
static md_parser_t g_parser;

/* ============================================================================
 * 发送辅助：把 mdc_lib 打包结果交给 Serial2（用户侧收发）
 * ==========================================================================*/
static void send_bin(const uint8_t *buf, uint16_t n)
{
    if (n > 0) {
        rcSerial.write(buf, n);
    }
}

/* ============================================================================
 * task_recv：读 Serial2 → 流式解析 → 0xF0 用 md_parse_status 解析打印
 * ==========================================================================*/
void task_recv(void *pv)
{
    (void)pv;
    while (1) {
        while (rcSerial.available()) {
            uint8_t b = (uint8_t)rcSerial.read();
            uint8_t cmd;
            const uint8_t *payload;
            uint16_t plen;

            /* 流式解析：自动找 0xAA 同步 + CRC 校验，完整帧返回 1 */
            if (md_parser_feed(&g_parser, b, &cmd, &payload, &plen)) {
                if (cmd == MD_CMD_STATUS_REPORT) {           /* 0xF0 状态上报 */
                    md_status_t st;
                    if (md_parse_status(payload, plen, &st)) {
                        /* payload 指向解析器内部缓冲，解析完成即消费 */
                        Serial.printf("[0xF0] enc=%ld,%ld,%ld,%ld tgt=%.1f,%.1f,%.1f,%.1f rpm=%ld,%ld,%ld,%ld\r\n",
                                      (long)st.enc[0], (long)st.enc[1], (long)st.enc[2], (long)st.enc[3],
                                      (double)st.tgt[0], (double)st.tgt[1], (double)st.tgt[2], (double)st.tgt[3],
                                      (long)st.rpm[0], (long)st.rpm[1], (long)st.rpm[2], (long)st.rpm[3]);
                    }
                } else if (plen == 1) {                      /* ACK 帧（如 0x40 应答） */
                    md_ack_t ack;
                    if (md_parse_ack(payload, plen, &ack)) {
                        Serial.printf("[ACK] cmd=0x%02X err=0x%02X\r\n", ack.cmd, ack.err);
                    }
                }
                /* 其它上报帧（0xF1/0xF2 等）按需扩展 */
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

/* ============================================================================
 * task_send：每 50ms 用 mdc_lib 打包并发送一帧 0x31 控制帧
 * （规范 §3.3 实时控制需连续发送，避免 /timeout 超时归零）
 * ==========================================================================*/
void task_send(void *pv)
{
    (void)pv;
    TickType_t last = xTaskGetTickCount();
    uint8_t frame[32];                     /* 0x31 帧 20B */

    while (1) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(CTRL_SEND_MS));
        uint16_t n = md_bin_motor_ctrl(g_target[0], g_target[1], g_target[2], g_target[3],
                                       frame, sizeof(frame));
        send_bin(frame, n);
    }
}

/* ============================================================================
 * USB Serial 命令处理
 * ==========================================================================*/
static void handle_usb_line(String line)
{
    line.trim();
    if (line.length() == 0) return;

    /* 1) 任意文本指令：以 / 开头 → md_text_build 构造（自动补 '\n'）后转发 */
    if (line.startsWith("/")) {
        char out[64];
        Serial.print(">>> "); Serial.println(line);
        uint16_t n = md_text_build(line.c_str(), NULL, out, sizeof(out));
        send_bin((const uint8_t *)out, n);
        return;
    }
    /* 2) mode 1 speed → md_text_mode 构造 /mode 1 speed\n 转发 */
    if (line.startsWith("mode ")) {
        String rest = line.substring(5);              /* "1 speed" 或 "1" */
        int sp = rest.indexOf(' ');
        int ch = (int)rest.substring(0, sp < 0 ? rest.length() : sp).toInt();
        String mode = (sp < 0) ? "" : rest.substring(sp + 1);
        char out[32];
        Serial.print(">>> /mode "); Serial.println(rest);
        uint16_t n = md_text_mode((uint8_t)ch,
                                  mode.length() ? mode.c_str() : NULL,
                                  out, sizeof(out));
        send_bin((const uint8_t *)out, n);
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

    md_parser_init(&g_parser);

    delay(200);
    Serial.println("=== Motor Driver Controller - ESP32 (mdc_lib) ===");
    Serial.println("命令: pwm <v> | speed <rpm> | stop | mode <ch> <mode> | /文本指令");

    /* 订阅状态上报（100ms），task_recv 会解析 0xF0 打印 */
    uint8_t frame[16];
    uint16_t n = md_bin_subscribe(SUBSCRIBE_MS, frame, sizeof(frame));
    send_bin(frame, n);

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
