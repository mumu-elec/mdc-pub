/*=============================================================================
 *  main.c - Motor Driver Controller host example for 8051 (Keil C51)
 *=============================================================================
 *  Target : AT89C52 / STC89C52 (Keil C51), 11.0592MHz crystal
 *  UART1  : mode 1 (8-bit variable baud rate), 9600 baud
 *           Timer1 mode 2 (8-bit auto reload), TH1 = TL1 = 0xFD
 *  Wiring : P3.1 (TXD) -> control board RC signal (RC RX)
 *           P3.0 (RXD) <- control board RC signal (RC TX)
 *           GND common ground
 *  Board pre-config (via USB serial tool, 2000000-8N1):
 *           /uart2 9600 0 uart
 *
 *  Protocol : common/协议规范.md (refer to it as the single source of truth)
 *    - Text commands are terminated by '\n' (spec section 2.1)
 *    - Binary frame : [SYNC=0xAA][CMD:1B][LEN:1B][DATA:0~250B][CRC8:1B]
 *    - CRC8 : poly 0x07, init 0, computed over CMD + LEN + DATA (not SYNC)
 *    - All multi-byte fields are little-endian (LE)
 *
 *  Features:
 *    1. UART1 RX interrupt collects one line; when a line is complete
 *       (terminated by '\r' or '\n') it is forwarded to the RC port.
 *    2. Demo functions send text commands: /version, /mode 1 speed,
 *       /speedctrl 1 0.5 0.02 0.01, /save.
 *    3. Optional binary frame demo send_motor_ctrl() (0x31 + CRC8,
 *       int32 little-endian built manually). Disabled by default,
 *       enable by setting SEND_CTRL_FRAME to 1.
 *=============================================================================*/

/*--------------------------- register definitions ---------------------------*/
sfr  P0     = 0x80;
sfr  P1     = 0x90;
sfr  P2     = 0xA0;
sfr  P3     = 0xB0;
sfr  PSW    = 0xD0;
sfr  ACC    = 0xE0;
sfr  SP     = 0x81;
sfr  TMOD   = 0x89;
sfr  TCON   = 0x88;
sfr  TH1    = 0x8D;
sfr  TL1    = 0x8B;
sfr  SCON   = 0x98;
sfr  SBUF   = 0x99;
sfr  PCON   = 0x87;
sfr  IE     = 0xA8;
sfr  IP     = 0xB8;

sbit RI  = SCON ^ 0;      /* receive interrupt flag */
sbit TI  = SCON ^ 1;      /* transmit interrupt flag */
sbit REN = SCON ^ 4;      /* receiver enable */
sbit SM0 = SCON ^ 7;      /* serial mode bit 0 */
sbit SM1 = SCON ^ 6;      /* serial mode bit 1 */
sbit TR1 = TCON ^ 6;      /* timer1 run control */
sbit ES  = IE   ^ 4;      /* serial interrupt enable */
sbit EA  = IE   ^ 7;      /* global interrupt enable */

/*--------------------------- protocol constants -----------------------------*/
#define FRAME_SYNC      0xAAu      /* binary frame sync byte (spec 3.1) */
#define CMD_MOTOR_CTRL  0x31u      /* MOTOR_CTRL: 4-channel control frame */
#define CMD_SUBSCRIBE   0x40u      /* SUBSCRIBE: periodic status report */

/*--------------------------- user options ----------------------------------*/
#define SEND_CTRL_FRAME 0          /* set to 1 to send 0x31 demo frame at boot */
#define RX_BUF_MAX      48         /* RX line buffer size */

/*--------------------------- global variables ------------------------------*/
static unsigned char rx_buf[RX_BUF_MAX];   /* received line buffer */
static unsigned char rx_len   = 0;         /* bytes in rx_buf */
static unsigned char line_ok  = 0;         /* 1 = a complete line is ready */

/*=============================================================================
 *  CRC8 : poly 0x07, init 0, range = CMD + LEN + DATA (not SYNC)
 *  Ported from the reference implementation in protocol spec section 3.1.
 *===========================================================================*/
unsigned char crc8(unsigned char *d, unsigned int len)
{
    unsigned char c = 0;
    unsigned int  i;
    unsigned char b;

    for (i = 0; i < len; i++) {
        c ^= d[i];
        for (b = 0; b < 8; b++)
            c = (c & 0x80) ? (unsigned char)((c << 1) ^ 0x07) : (unsigned char)(c << 1);
    }
    return c;
}

/*=============================================================================
 *  Low-level UART helpers
 *===========================================================================*/
/* send one byte, blocking until TI is set */
void send_char(unsigned char ch)
{
    while (!TI);
    TI  = 0;
    SBUF = ch;
}

/* send a NUL-terminated string without terminator */
void send_str(unsigned char *s)
{
    while (*s) {
        send_char(*s++);
    }
}

/* send a text command with the required '\n' terminator (spec 2.1) */
void send_line(unsigned char *s)
{
    send_str(s);
    send_char('\n');
}

/* approximate delay, about 1ms per count at 11.0592MHz (tune if needed) */
void delay_ms(unsigned int ms)
{
    unsigned int i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 120; j++);
}

/*=============================================================================
 *  Binary frame helpers
 *===========================================================================*/
/* append int32 little-endian (manual byte shifting, no union alignment) */
static void put_i32_le(unsigned char *buf, unsigned char *idx, long v)
{
    buf[(*idx)++] = (unsigned char)(v & 0xFF);
    buf[(*idx)++] = (unsigned char)((v >> 8) & 0xFF);
    buf[(*idx)++] = (unsigned char)((v >> 16) & 0xFF);
    buf[(*idx)++] = (unsigned char)((v >> 24) & 0xFF);
}

/* send one binary frame: [0xAA][CMD][LEN][DATA...][CRC8]
 * NOTE: frame[] lives in the default data space (small model, 128B on
 * AT89C52). It is sized for the demo frames below (16B max DATA).
 * For large payloads (e.g. config_t 231B) move the buffer to xdata or
 * use a large-RAM part (STC89C52RC: xdata via AUXR). */
void send_frame(unsigned char cmd, unsigned char *data, unsigned char len)
{
    unsigned char frame[24];
    unsigned int  i;

    frame[0] = FRAME_SYNC;
    frame[1] = cmd;
    frame[2] = len;
    for (i = 0; i < len; i++) {
        frame[3 + i] = data[i];
    }
    /* CRC8 range = CMD + LEN + DATA, i.e. frame[1..2+len], length = 2 + len */
    frame[3 + len] = crc8(&frame[1], (unsigned int)(2 + len));
    for (i = 0; i < (unsigned int)(4 + len); i++) {
        send_char(frame[i]);
    }
}

/* send 0x31 MOTOR_CTRL demo: open-loop PWM 400 / -400 on ch1/ch2 (LE int32).
 * Meaning of target depends on channel mode: open = PWM(+-1000),
 * speed = RPM, pos = 0.1 deg (+-3600). Real-time control should send
 * this frame continuously every 30/50/100ms (spec 3.3). */
void send_motor_ctrl_demo(void)
{
    unsigned char data[16];
    unsigned char idx = 0;
    long targets[4];

    targets[0] = 400L;      /* ch1: open-loop forward PWM 400 */
    targets[1] = -400L;     /* ch2: open-loop reverse PWM -400 */
    targets[2] = 0L;
    targets[3] = 0L;

    put_i32_le(data, &idx, targets[0]);
    put_i32_le(data, &idx, targets[1]);
    put_i32_le(data, &idx, targets[2]);
    put_i32_le(data, &idx, targets[3]);

    send_frame(CMD_MOTOR_CTRL, data, 16);
}

/* send 0x40 SUBSCRIBE demo: status report every 100ms (2B LE interval) */
void send_subscribe_demo(void)
{
    unsigned char data[2];
    data[0] = 100u & 0xFFu;
    data[1] = (100u >> 8) & 0xFFu;
    send_frame(CMD_SUBSCRIBE, data, 2);
}

/*=============================================================================
 *  Demo text commands (spec section 2)
 *===========================================================================*/
void demo_send_text_cmds(void)
{
    send_line("/version");                        /* query version */
    delay_ms(50);
    send_line("/mode 1 speed");                   /* ch1 -> speed closed loop */
    delay_ms(50);
    send_line("/speedctrl 1 0.5 0.02 0.01");      /* ch1 speed PID kp/ki/kd */
    delay_ms(50);
    send_line("/save");                           /* persist RAM config */
}

/*=============================================================================
 *  UART1 ISR: collect bytes into rx_buf; on '\r' or '\n' mark line complete.
 *  A complete line is then forwarded to the RC port in main loop.
 *===========================================================================*/
void uart_isr(void) interrupt 4
{
    unsigned char ch;

    if (RI) {
        RI = 0;
        ch = SBUF;
        if (ch == '\n' || ch == '\r') {
            if (rx_len > 0) {
                line_ok = 1;      /* line complete, main loop will forward it */
            }
        } else if (rx_len < (RX_BUF_MAX - 1)) {
            rx_buf[rx_len++] = ch;
        }
    }
    if (TI) {
        TI = 0;
    }
}

/* forward the received line to the RC port (transparent forwarding).
 * NOTE: if the control board's text replies are echoed back into the
 *       8051 RX (loop-back test setup), they will be forwarded again and
 *       may form a loop. To avoid this, either comment out the call in
 *       main(), or only forward lines starting with '/':
 *           if (rx_buf[0] == '/') forward_line(); */
void forward_line(void)
{
    unsigned char i;

    for (i = 0; i < rx_len; i++) {
        send_char(rx_buf[i]);
    }
    send_char('\n');              /* protocol requires '\n' terminator */
    rx_len = 0;
    line_ok = 0;
}

/*=============================================================================
 *  UART1 init: mode 1 (8-bit variable baud), 9600 baud @ 11.0592MHz
 *    SMOD = 0, baud = 11059200 / (32 * 12 * (256 - TH1)) = 9600
 *    -> 256 - TH1 = 3, TH1 = TL1 = 0xFD
 *===========================================================================*/
void uart_init(void)
{
    SCON = 0x50;      /* SM0=0 SM1=1: mode 1; REN=1: enable receive */
    PCON = 0x00;      /* SMOD = 0 */
    TMOD = 0x20;      /* Timer1 mode 2 (8-bit auto reload) */
    TH1  = 0xFD;      /* 9600 baud @ 11.0592MHz */
    TL1  = 0xFD;
    TR1  = 1;         /* start Timer1 */
    ES   = 1;         /* enable serial interrupt */
    EA   = 1;         /* enable global interrupt */
}

/*=============================================================================
 *  main
 *===========================================================================*/
void main(void)
{
    uart_init();

    /* demo: send preset text commands once at boot (replace with button /
     * timer trigger if you prefer) */
    demo_send_text_cmds();

#if SEND_CTRL_FRAME
    /* optional demo: binary control frame 0x31 + 0x40 subscribe.
     * To enable: set SEND_CTRL_FRAME to 1 above and recompile. */
    send_subscribe_demo();
    send_motor_ctrl_demo();
#endif

    while (1) {
        /* forward any complete received line to the RC port */
        if (line_ok) {
            forward_line();
        }
        /* other application logic can be added here */
    }
}
