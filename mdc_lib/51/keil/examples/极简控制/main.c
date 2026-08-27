/*=============================================================================
 *  main.c - 51 example: minimal control (send-only) using mdc_lite
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
 *  This example demonstrates ONLY the send-only side of mdc_lite (LITE.md 2):
 *  build a 0x31 MOTOR_CTRL frame with md_lite_ctrl() and send it. It does NOT
 *  parse any reply - if you need the four channel RPM back from the 0xF0
 *  STATUS_REPORT, use the 控制+回调 example (mdc_lite_ctrl).
 *
 *  Protocol layer is provided entirely by mdc_lib/mdc_lite:
 *    md_lite_ctrl / md_lite_stop / md_lite_subscribe / md_lite_unsubscribe
 *  This file only implements the UART I/O (user side).
 *
 *  Tuning: MD_ENABLE_CONFIG=0 and MD_PARSER_BUF=64 must be set before the
 *  include; for the same effect in mdc_lib.c add MD_ENABLE_CONFIG=0 to the
 *  Keil project defines (see README.md).
 *=============================================================================*/

#define MD_ENABLE_CONFIG 0

#include "mdc_lite.h"

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
#define SEND_PERIOD_MS 50     /* real-time control period (spec 3.3: 30/50/100ms) */

/* mdc_lite pack output buffer. A 0x31 control frame is exactly 20 bytes, so
 * 20B is enough. Keep it in the default data space (small model); on
 * large-RAM parts (STC89C52RC) you may move it to xdata. */
static unsigned char tx_buf[20];

/*=============================================================================
 *  Low-level UART helpers (user side)
 *===========================================================================*/
void send_char(unsigned char ch)
{
    while (!TI);
    TI  = 0;
    SBUF = ch;
}

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
}

/*=============================================================================
 *  main - send-only demo
 *===========================================================================*/
void main(void)
{
    unsigned int n;

    uart_init();
    delay_ms(100);                 /* let the board finish its boot */

    /* Optional: 0x40 SUBSCRIBE enables periodic status reporting. This
     * send-only example does not parse the replies; you need the
     * mdc_lite_ctrl (控制+回调) example to consume them. Comment it out if
     * you only want to drive the motors. */
    n = md_lite_subscribe(50, tx_buf, sizeof(tx_buf));
    send_packed(tx_buf, n);

    /* Send one control frame: 0x31 MOTOR_CTRL. Target meaning per channel
     * control mode: open = PWM(+/-1000), speed = RPM, pos = 0.1 deg.
     * Here ch1 = open-loop 300, ch3 = open-loop -150 (ch2/ch4 idling). */
    n = md_lite_ctrl(300, 0, -150, 0, tx_buf, sizeof(tx_buf));
    send_packed(tx_buf, n);

    /* Real-time loop: keep sending the set-point every SEND_PERIOD_MS.
     * Replace the target arguments with your own values / a variable. */
    while (1) {
        n = md_lite_ctrl(300, 0, -150, 0, tx_buf, sizeof(tx_buf));
        send_packed(tx_buf, n);
        delay_ms(SEND_PERIOD_MS);
    }
}
