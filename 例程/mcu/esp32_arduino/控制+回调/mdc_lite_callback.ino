/*
 * ============================================================================
 *  Motor Driver Controller — ESP32 控制+回调例程（mdc_lite_ctrl）
 * ============================================================================
 *  在 mdc_lite（只管发送）基础上，增加流式接收 0xF0 并把四通道 rpm 回调给用户。
 *  硬件：ESP32 DevKit（如 ESP32-WROOM-32）
 *  连接：HardwareSerial Serial2(16, 17) 接控制板 RC 接口（USART2）
 *        - ESP32 RX = 16 ← 控制板 RC TX；ESP32 TX = 17 → 控制板 RC RX；GND 共地
 *  波特率：115200（使用前请先在控制板上执行 /uart2 115200 0 uart）
 *
 *  本文件只做串口收发；打包（md_lite_*）与回调解析（md_lite_ctrl_feed）由库完成：
 *    - md_lite_ctrl(m0,m1,m2,m3,out,cap)    → 0x31 控制帧（发送）
 *    - md_lite_subscribe(ms,out,cap)        → 0x40 订阅（收速度的前提）
 *    - md_lite_ctrl_init(&g_ctrl, on_speed) → 注册速度回调
 *    - md_lite_ctrl_feed(&g_ctrl, byte)     → 喂字节；0xF0 到达时自动回调 on_speed(rpm[4])
 * ============================================================================
 */

#include "mdc_lite_ctrl.h"      /* 同时获得 send-only + 回调接收 */
#include <HardwareSerial.h>

/* ---- RC 口 UART（HardwareSerial( rxPin, txPin )）---- */
#define RC_RX_PIN  16    /* ESP32 RX ← 控制板 RC TX */
#define RC_TX_PIN  17    /* ESP32 TX → 控制板 RC RX */
HardwareSerial rcSerial(2);                 /* UART2 外设 */

#define CTRL_SEND_MS  50       /* 控制帧发送周期（规范建议 30/50/100ms） */
#define SUBSCRIBE_MS  100      /* 状态上报间隔（固件钳位 ≥20ms） */

/* 极简控制器实例（内含流式解析器 256B，全局/static 避免占栈） */
static md_lite_ctrl_t g_ctrl;

/* 速度回调：0xF0 STATUS_REPORT 到达时被调用，rpm 为四通道实时转速 */
static void on_speed(const int32_t rpm[4])
{
    Serial.printf("[rpm] %ld,%ld,%ld,%ld\r\n",
                  (long)rpm[0], (long)rpm[1], (long)rpm[2], (long)rpm[3]);
}

/* 发送辅助：把 mdc_lite 打包结果交给串口（用户侧收发） */
static void tx_bin(const uint8_t *buf, uint16_t n)
{
    if (n > 0) {
        rcSerial.write(buf, n);
    }
}

void setup()
{
    Serial.begin(115200);        /* USB 串口监视器 */
    rcSerial.begin(115200, SERIAL_8N1, RC_RX_PIN, RC_TX_PIN);   /* RC 口 */

    md_lite_ctrl_init(&g_ctrl, on_speed);   /* 注册速度回调（NULL 则不派发） */

    Serial.println(F("=== Motor Driver Controller - ESP32 (mdc_lite 控制+回调) ==="));

    /* 先订阅状态上报（收速度的前提）；回调在 0xF0 到达时触发 */
    uint8_t f[16];
    uint16_t n = md_lite_subscribe(SUBSCRIBE_MS, f, sizeof(f));
    tx_bin(f, n);
}

void loop()
{
    /* ① 发送：每 50ms 连续发送 0x31 控制帧（避免 /timeout 归零） */
    static uint32_t last = 0;
    if (millis() - last >= CTRL_SEND_MS) {
        last = millis();
        uint8_t frame[32];
        /* 例：ch1 目标 400（开环 PWM 或速度 RPM，配合控制板 /mode 1 open|speed） */
        uint16_t n = md_lite_ctrl(400, 0, 0, 0, frame, sizeof(frame));
        tx_bin(frame, n);
    }

    /* ② 接收：RC 口字节逐字节喂给回调接收器（0xF0 到达时自动回调 on_speed） */
    while (rcSerial.available() > 0) {
        uint8_t b = (uint8_t)rcSerial.read();
        md_lite_ctrl_feed(&g_ctrl, b);
    }
}
