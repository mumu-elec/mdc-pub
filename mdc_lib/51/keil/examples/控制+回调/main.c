/*=============================================================================
 *  main.c - 51 example: control + speed callback using mdc_lite_ctrl
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
 *  This example demonstrates mdc_lite_ctrl (LITE.md 3): it sends 0x31
 *  MOTOR_CTRL frames AND receives the 0xF0 STATUS_REPORT speed callback.
 *  The UART RX interrupt feeds each received byte into md_lite_ctrl_feed();
 *  when a complete STATUS_REPORT (56B/72B) arrives, md_lite_ctrl parses the
 *  four channel RPM and calls on_speed().
 *
 *  Protocol layer is provided entirely by mdc_lite (self-contained):
 *    send:  md_lite_ctrl / md_lite_stop / md_lite_subscribe / md_lite_unsubscribe
 *    recv:  md_lite_ctrl_init / md_lite_ctrl_feed  (library-internal streaming
 *           0xF0 parser + on_speed callback, does NOT depend on mdc_lib)
 *  This file only implements the UART I/O (user side).
 *
 *  Tuning: mdc_lite keeps its RX buffer as a static in mdc_lite_ctrl.c; nothing
 *  to configure here. The 72B 0xF0 STATUS_REPORT frame is ~76B, which the
 *  library's internal buffer already covers.
 *  IMPORTANT: only one receiver is supported (single static buffer); do NOT
 *  call md_lite_ctrl_init with the same instance from multiple places.
 *=============================================================================*/

#include "mdc_lite_ctrl.h"

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
#define SEND_PERIOD_MS 50     /* real-time control period (spec 3.3) */

/* UART state. tx_buf fits a 0x31 frame (20B). The mdc_lite_ctrl receiver holds
 * its own static xdata buffer internally, so the user only needs tx_buf here. */
xdata unsigned char tx_buf[20];

/* This counter is shown only as a trivial side-effect of the callback; on a
 * real application you would use the RPM (e.g. a control loop / telemetry). */
static unsigned int g_speed_packets;

/*=============================================================================
 *  Speed callback (LITE.md 3): four channel RPM from a 0xF0 STATUS_REPORT.
 *  `rpm[0..3]` point into the library's internal status buffer and are only
 *  valid for this call; copy the values if you must keep them. Called from
 *  the UART interrupt context, so keep it short.
 *===========================================================================*/
void on_speed(const int32_t rpm[4])
{
    g_speed_packets++;            /* demo: count status packets */
    /* use rpm[0]..rpm[3] here (e.g. display / store / act on them) */
}

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
 *  UART1 RX ISR: feed every received byte into the mdc_lite_ctrl receiver.
 *  A complete 0xF0 STATUS_REPORT triggers on_speed(); all other frames/noise
 *  are ignored by the library.
 *===========================================================================*/
void uart_isr(void) interrupt 4
{
    if (RI) {
        RI = 0;
        md_lite_ctrl_feed(SBUF);
    }
    if (TI) {
        TI = 0;
    }
}

/*=============================================================================
 *  main - send control frames + receive speed callback
 *===========================================================================*/
void main(void)
{
    unsigned int n;

    uart_init();

    /* Register the speed callback and initialise the streaming parser.
     * Call this once, before feeding any bytes. */
    md_lite_ctrl_init(on_speed);

    delay_ms(100);

    /* 0x40 SUBSCRIBE is the prerequisite for receiving STATUS_REPORT speed.
     * interval_ms = 50 (firmware clamps to >= 20). */
    n = md_lite_subscribe(50, tx_buf, sizeof(tx_buf));
    send_packed(tx_buf, n);

    /* Real-time loop: keep sending a 0x31 MOTOR_CTRL set-point; the UART RX
     * interrupt handles the speed callback in the background. */
    while (1) {
        n = md_lite_ctrl(100, 0, 0, 0, tx_buf, sizeof(tx_buf));
        send_packed(tx_buf, n);
        delay_ms(SEND_PERIOD_MS);
        /* g_speed_packets is updated by the callback and could be displayed
         * or used to drive other logic here. */
    }
}
