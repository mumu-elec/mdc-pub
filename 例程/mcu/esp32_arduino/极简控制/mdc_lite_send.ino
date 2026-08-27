/*
 * ============================================================================
 *  Motor Driver Controller — ESP32 极简控制例程（mdc_lite send-only）
 * ============================================================================
 *  只用 mdc_lite：只打包"要发送的控制帧"，不解析任何回包。
 *  硬件：ESP32 DevKit（如 ESP32-WROOM-32）
 *  连接：HardwareSerial Serial2(16, 17) 接控制板 RC 接口（USART2）
 *        - ESP32 RX = 16 ← 控制板 RC TX；ESP32 TX = 17 → 控制板 RC RX；GND 共地
 *  波特率：115200（使用前请先在控制板上执行 /uart2 115200 0 uart）
 *
 *  本文件只做串口收发；打包（0x31/0x40）全部由 mdc_lite 完成：
 *    - md_lite_ctrl(m0,m1,m2,m3, out, cap)   → 0x31 四通道控制帧（20B，含 SYNC+CRC8）
 *    - md_lite_subscribe(ms, out, cap)       → 0x40 订阅状态上报（6B）
 *    - md_lite_stop(out, cap)                → 0x31 全零（急停/退出）
 *    - md_lite_unsubscribe(out, cap)         → 0x41 关闭上报（善后）
 *  需要回读实时转速 → 请改用同目录「控制+回调」例程（mdc_lite_ctrl）。
 * ============================================================================
 */

#include "mdc_lite.h"
#include <HardwareSerial.h>

/* ---- RC 口 UART（HardwareSerial( rxPin, txPin )）---- */
#define RC_RX_PIN  16    /* ESP32 RX ← 控制板 RC TX */
#define RC_TX_PIN  17    /* ESP32 TX → 控制板 RC RX */
HardwareSerial rcSerial(2);                 /* UART2 外设 */

#define CTRL_SEND_MS  50       /* 控制帧发送周期（规范建议 30/50/100ms） */
#define SUBSCRIBE_MS  100      /* 状态上报间隔（固件钳位 ≥20ms） */

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

    Serial.println(F("=== Motor Driver Controller - ESP32 (mdc_lite 极简控制/send-only) ==="));
    Serial.println(F("仅发送控制帧，不解析回包；如需回读转速请用「控制+回调」例程"));

    /* 订阅状态上报：本次只发不解析，此处仅为让下位机进入上报态（可选，善后可取消） */
    uint8_t f[16];
    uint16_t n = md_lite_subscribe(SUBSCRIBE_MS, f, sizeof(f));
    tx_bin(f, n);
}

void loop()
{
    /* 每 50ms 连续发送 0x31 控制帧（规范 §3.3 实时控制需连续发送，避免 /timeout 归零）
     * 目标值含义取决于各通道模式：开环=PWM(±1000)，速度=RPM，位置=0.1° */
    static uint32_t last = 0;
    if (millis() - last >= CTRL_SEND_MS) {
        last = millis();
        uint8_t frame[32];
        /* 例：ch1 目标 400（开环 PWM 或速度 RPM，配合控制板 /mode 1 open|speed） */
        uint16_t n = md_lite_ctrl(400, 0, 0, 0, frame, sizeof(frame));
        tx_bin(frame, n);
    }
}
