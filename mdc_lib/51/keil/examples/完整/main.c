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
 *  Protocol layer is provided entirely by mdc_lib (mdc_lib/51/keil/):
 *    - Text commands are built with md_text_build() (adds the '\n'
 *      terminator); the returned bytes are sent one by one via send_char().
 *    - Optional binary frames are built with md_bin_motor_ctrl() and
 *      md_bin_subscribe() (CRC8 and framing live inside mdc_lib).
 *    - This file only implements the UART I/O (user side).
 *
 *  Features:
 *    1. UART1 RX interrupt collects one line; when a line is complete
 *       (terminated by '\r' or '\n') it is forwarded to the RC port.
 *    2. Demo functions send text commands built by mdc_lib: /version,
 *       /mode 1 speed, /speedctrl 1 0.5 0.02 0.01, /save.
 *    3. Optional binary frame demo: md_bin_motor_ctrl() (0x31) +
 *       md_bin_subscribe() (0x40). Disabled by default, enable by
 *       setting SEND_CTRL_FRAME to 1.
 *=============================================================================*/

/* mdc_lib tuning for 8051 (MUST be defined before #include "mdc_lib.h"):
 *   MD_ENABLE_CONFIG=0 compiles out the config full-field functions and the
 *   231B xdata scratch buffer (this example does not use them).
 *   For the same effect in mdc_lib.c, also add MD_ENABLE_CONFIG=0 to the
 *   Keil project defines (Options for Target -> C51 -> Preprocessor
 *   Symbols -> Define), see README.md. */
#define MD_ENABLE_CONFIG 0

#include "mdc_lib.h"

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

/*--------------------------- user options ----------------------------------*/
#define SEND_CTRL_FRAME 0          /* set to 1 to send 0x31/0x40 demo frames at boot */
#define RX_BUF_MAX      32         /* RX line buffer size (internal RAM is tight) */

/*--------------------------- global variables ------------------------------*/
static unsigned char rx_buf[RX_BUF_MAX];   /* received line buffer */
static unsigned char rx_len   = 0;         /* bytes in rx_buf */
static unsigned char line_ok  = 0;         /* 1 = a complete line is ready */
/* mdc_lib pack output buffer. 40B fits the demo text lines (30B max) and
 * the 0x31 binary frame (20B). Keep it in the default data space (small
 * model); on large-RAM parts (STC89C52RC) you may move it to xdata. */
static unsigned char tx_buf[40];

/*=============================================================================
 *  Low-level UART helpers (user side)
 *===========================================================================*/
/* send one byte, blocking until TI is set */
void send_char(unsigned char ch)
{
    while (!TI);
    TI  = 0;
    SBUF = ch;
}

/* send the bytes produced by an mdc_lib pack function */
void send_packed(const unsigned char *buf, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i++) {
        send_char(buf[i]);
    }
}

/* approximate delay, about 1ms per count at 11.0592MHz (tune if needed) */
void delay_ms(unsigned int ms)
{
    unsigned int i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 120; j++);
}

/*=============================================================================
 *  Text commands: built by mdc_lib into tx_buf, then sent byte by byte.
 *  md_text_build(cmd, args, out, cap) returns bytes written (excludes NUL);
 *  args = 0 means "no arguments" (read mode, e.g. "/version\n").
 *===========================================================================*/
void send_text_cmd(const char *cmd, const char *args)
{
    unsigned int n = md_text_build(cmd, args, (char *)tx_buf, sizeof(tx_buf));
    send_packed(tx_buf, n);
}

/*=============================================================================
 *  Demo text commands (spec section 2)
 *===========================================================================*/
void demo_send_text_cmds(void)
{
    send_text_cmd("/version", 0);                    /* query version */
    delay_ms(50);
    send_text_cmd("/mode", "1 speed");               /* ch1 -> speed closed loop */
    delay_ms(50);
    send_text_cmd("/speedctrl", "1 0.5 0.02 0.01");  /* ch1 speed PID kp/ki/kd */
    delay_ms(50);
    send_text_cmd("/save", 0);                       /* persist RAM config */
}

/*=============================================================================
 *  Optional binary frame demo (mdc_lib packs the whole frame incl. CRC8)
 *===========================================================================*/
#if SEND_CTRL_FRAME
/* send 0x31 MOTOR_CTRL: open-loop PWM 400 / -400 on ch1/ch2 (LE int32).
 * Meaning of target depends on channel mode: open = PWM(+-1000),
 * speed = RPM, pos = 0.1 deg (+-3600). Real-time control should send
 * this frame continuously every 30/50/100ms (spec 3.3). */
void send_motor_ctrl_demo(void)
{
    unsigned int n = md_bin_motor_ctrl(400L, -400L, 0L, 0L, tx_buf, sizeof(tx_buf));
    send_packed(tx_buf, n);
}

/* send 0x40 SUBSCRIBE demo: status report every 100ms (2B LE interval) */
void send_subscribe_demo(void)
{
    unsigned int n = md_bin_subscribe(100u, tx_buf, sizeof(tx_buf));
    send_packed(tx_buf, n);
}
#endif /* SEND_CTRL_FRAME */

/*=============================================================================
 *  UART1 ISR: collect bytes into rx_buf; on '\r' or '\n' mark line complete.
 *  A complete line is then forwarded to the RC port in main loop.
 *  NOTE: to also parse binary reports (0xF0 STATUS_REPORT etc.), declare
 *  "xdata md_parser_t g_parser;" and feed every received byte into
 *  md_parser_feed() here, then use md_parse_status() when it returns 1.
 *  See mdc_lib/51/keil/README.md (parser buffer needs xdata; 256B default).
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
