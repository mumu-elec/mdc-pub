/*
 * mdc_mcu_example.ino — 外部 MCU（Arduino 框架）控制 MDC 电机驱动器
 *
 * 对应《技术手册》§3.3（RC 接口）/ §5.2（文本指令）/ §5.5（二进制帧 / MOTOR_CTRL）。
 *
 * 接线（§3.3）：
 *   外部 MCU TX  → MDC RC 接口 SIGNAL 脚
 *   外部 MCU GND → MDC RC 接口 GND（必须共地）
 *   RC 接口无极性（可反插）；板上异或门中继，3.3V / 5V TTL 均可；
 *   RC 只传信号不供电——MDC 需接 DC 电源，接收方 MCU 用自己的电源。
 *
 * 串口参数：MDC 出厂默认 UART 模式 / 115200 8N1。
 *   若曾配置为 SBUS/ELRS：长按 FUN 键 1s 重新识别，或发 /uart2 115200 0 uart。
 *   波特率可改（1200~4000000），改后需 /save 持久化。
 *
 * 硬件串口推荐（115200 下 SoftwareSerial 易丢字节）；本例用 Serial1，
 * UNO/Nano 无 Serial2/3，请换用硬件串口足够的板子（Mega/ESP32 等）。
 */

/* ---------------- 选择演示方式 ---------------- */
#define DEMO_TEXT   0   /* 文本指令：直观，适合低速/调试 */
#define DEMO_BINARY 1   /* 二进制 MOTOR_CTRL (0x31)：紧凑，适合高频率控制环 */
#define DEMO_MODE   DEMO_BINARY

static const unsigned long MDC_BAUD = 115200;

/* 若固件按上位机/遥控优先级仲裁，控制权可能被另一端口抢占；
 * 上电后发一次超时保护配置更稳妥（RAM 生效；要持久化加 "/save"）。 */
static const char *SETUP_CMDS[] = {
  "/timeout 1000",      /* 1s 无控制指令 → 电机自动归零（出厂默认 0=关闭保护） */
  "/mode 1 speed",      /* 通道 A 速度闭环（无编码器请改 /mode 1 open，目标即 PWM ±1000） */
};
static const int NUM_SETUP_CMDS = sizeof(SETUP_CMDS) / sizeof(SETUP_CMDS[0]);

/* ---------------- CRC8（多项式 0x07，初值 0x00，覆盖 CMD+LEN+DATA，§5.3） ---------------- */
static uint8_t mdc_crc8(const uint8_t *d, int len) {
  uint8_t c = 0;
  for (int i = 0; i < len; i++) {
    c ^= d[i];
    for (uint8_t b = 0; b < 8; b++)
      c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
  }
  return c;
}

/* 发送一条文本指令（§5.2，回复以 "> " 结尾，本例不解析回复） */
static void mdc_cmd(const char *line) {
  Serial1.print(line);
  Serial1.print('\n');
  delay(50);                       /* 给固件处理与回显留时间 */
  while (Serial1.available()) Serial.read();  /* 丢弃回复（调试时可转发到 Serial 查看） */
}

/* 发送 MOTOR_CTRL (0x31)：底盘模式 p1=vx(mm/s) p2=vy(mm/s) p3=ω(0.001rad/s)；
 * 单电机模式 p1~p4 = 通道 A~D 各自目标（开环=PWM / 速度=RPM / 位置=0.1°）。
 * 本命令无 ACK（§5.5），建议以固定周期持续发送。 */
static void mdc_motor_ctrl(int32_t p1, int32_t p2, int32_t p3, int32_t p4) {
  uint8_t f[4 + 16];
  f[0] = 0xAA; f[1] = 0x31; f[2] = 16;
  for (int i = 0; i < 4; i++) {            /* int32 小端序 */
    int32_t v = (i == 0) ? p1 : (i == 1) ? p2 : (i == 2) ? p3 : p4;
    f[3 + i * 4 + 0] = (uint8_t)(v & 0xFF);
    f[3 + i * 4 + 1] = (uint8_t)((v >> 8) & 0xFF);
    f[3 + i * 4 + 2] = (uint8_t)((v >> 16) & 0xFF);
    f[3 + i * 4 + 3] = (uint8_t)((v >> 24) & 0xFF);
  }
  f[19] = mdc_crc8(&f[1], 18);             /* CRC 覆盖 CMD+LEN+DATA */
  Serial1.write(f, sizeof(f));
}

void setup() {
  Serial.begin(115200);        /* 调试口 */
  Serial1.begin(MDC_BAUD);     /* → MDC RC 接口 */
  delay(300);                  /* 等 MDC 上电初始化（LED 三快闪→常亮，§6.2） */

  for (int i = 0; i < NUM_SETUP_CMDS; i++) mdc_cmd(SETUP_CMDS[i]);
}

void loop() {
#if DEMO_MODE == DEMO_TEXT
  /* 文本方式适合低速配置与调试。注意：运动目标本身没有文本指令——
   * 目标只能来自二进制 0x30/0x31 帧或遥控器；
   * 下面演示“文本配置 + 0x30 单通道 PWM 直驱（旁路 PID，§5.5）”：
   * 帧格式 [0xAA][0x30][4][ch][dir][pwm_lo][pwm_hi][crc8]，ch=0~3，dir 0/1，pwm 0~1000 */
  mdc_cmd("/mode 1 open");           /* 开环档 */
  delay(1000);
  {
    uint8_t f[8] = { 0xAA, 0x30, 4, 0, 0, 0x2C, 0x01, 0 };  /* 通道0 正转 PWM=300 (0x012C 小端) */
    f[7] = mdc_crc8(&f[1], 6);
    Serial1.write(f, sizeof(f));
  }
  delay(1000);
  {
    uint8_t f[8] = { 0xAA, 0x30, 4, 0, 0, 0x00, 0x00, 0 };  /* 停 */
    f[7] = mdc_crc8(&f[1], 6);
    Serial1.write(f, sizeof(f));
  }
  delay(1000);

#else   /* DEMO_BINARY：控制环示例 —— 通道 A 300 RPM 转停交替 */
  static uint32_t t0 = 0;
  static bool on = false;
  if (millis() - t0 > 2000) {          /* 每 2s 切换一次目标 */
    t0 = millis();
    on = !on;
  }
  /* 控制帧建议 30~100ms 周期持续发送（与上位机一致，§5.5） */
  mdc_motor_ctrl(on ? 300 : 0, 0, 0, 0);
  delay(50);
#endif
}
