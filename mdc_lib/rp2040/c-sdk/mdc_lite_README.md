# mdc_lite — RP2040 C SDK 平台（纯 C 极简调用库）

> **目录：** `mdc_lib/rp2040/c-sdk/`（与 `mdc_lib.h/.c` 同目录，但本极简层**不依赖**它）
> **定位：** 只做「**告诉下位机发什么控制量、从下位机拿回实时转速**」的**独立极简调用库**。
> **只涉及 4 条二进制命令：** `0x31` 控制、`0x40/0x41` 订阅/退订、`0xF0` 状态上报（速度回调）。
> **规范依据：** [`../LITE.md`](../LITE.md)；**独立实现：** `mdc_lite.*` 自带 CRC8、组帧与流式解析，
> **不 include "mdc_lib.h"**、不调用 `md_bin_motor_*`/`md_parser_*` 等完整库原语；只用到 `<stdint.h>`，
> 帧字节布局与 `mdc_lib` 完全一致。

---

## 一、功能

- **零 pico-sdk 依赖**：`mdc_lite.c` / `mdc_lite_ctrl.c` 只 include `<stdint.h>`，不碰 `pico/stdlib.h`、`hardware/uart.h`，任何 C 工具链都能编译。
- **send-only**（`mdc_lite`）：只打包控制帧。
  - `md_lite_ctrl(m0..m3, out, cap)`：0x31 四通道控制帧。
  - `md_lite_stop`：`ctrl(0,0,0,0)` 全零控制帧（急停/退出）。
  - `md_lite_subscribe(ms,out,cap)`：0x40 开启状态上报；`md_lite_unsubscribe(out,cap)`：0x41 关闭。
- **control + 回调**（`mdc_lite_ctrl`）：`md_lite_ctrl_t` + `md_lite_ctrl_init(c,cb)` + `md_lite_ctrl_feed(c,byte)`；逐字节喂入，收 `0xF0`（56B/72B 自动兼容）提取 `rpm[4]` 调用回调。
- **不碰串口**：库只返回字节 / 喂入解析；串口收发由你通过 `hardware/uart.h` 实现。

## 二、API 速览

```c
/* send-only (mdc_lite.h) */
uint16_t md_lite_ctrl(int32_t m0,int32_t m1,int32_t m2,int32_t m3, uint8_t* out, uint16_t cap);
uint16_t md_lite_stop(uint8_t* out, uint16_t cap);
uint16_t md_lite_subscribe(uint16_t interval_ms, uint8_t* out, uint16_t cap);
uint16_t md_lite_unsubscribe(uint8_t* out, uint16_t cap);

/* control + speed callback (mdc_lite_ctrl.h) */
typedef void (*md_lite_on_speed_t)(const int32_t rpm[4]);
typedef struct { uint8_t buf[MD_LITE_BUF_SIZE]; uint16_t len; md_lite_on_speed_t on_speed; } md_lite_ctrl_t;
void md_lite_ctrl_init(md_lite_ctrl_t* c, md_lite_on_speed_t cb);
void md_lite_ctrl_feed(md_lite_ctrl_t* c, uint8_t byte);
```

> 验证向量：`md_lite_ctrl(100,-200,0,300)` DATA 段 `== 64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00`；
> `md_lite_subscribe(50)` 帧 `== AA 40 02 32 00 9E`；`md_lite_unsubscribe()` 帧 `== AA 41 00 4E`。

## 三、串口接入示例（RP2040 C SDK）

**① 只用发送（send-only）：** 配套最小示例见 `examples/mcu/rp2040_csdk/极简控制/`。

```c
#include "mdc_lite.h"
#include "hardware/uart.h"

static void user_send(const uint8_t* buf, uint16_t n)
{
    uart_write_blocking(uart0, buf, n);          /* 串口由你实现 */
}

void send_demo(void)
{
    uint8_t  buf[32];
    uint16_t n = md_lite_subscribe(50, buf, sizeof(buf));
    user_send(buf, n);                           /* AA 40 02 32 00 9E */
    n = md_lite_ctrl(100, -200, 0, 300, buf, sizeof(buf));
    user_send(buf, n);                           /* 0x31 四通道控制帧 */
}
```

**② 发送 + 速度回调（control + callback）：** 配套最小示例见 `examples/mcu/rp2040_csdk/控制+回调/`。

```c
#include "mdc_lite_ctrl.h"
#include "hardware/uart.h"
#include "hardware/irq.h"

static md_lite_ctrl_t g_lite;                    /* 静态区，避免占栈 */

static void on_speed(const int32_t rpm[4])
{
    (void)rpm;                                   /* 使用 rpm[0..3] */
}

/* UART0 接收中断：逐字节喂接收器，0xF0 到达自动回调 */
static void on_uart_rx(void)
{
    while (uart_is_readable(uart0))
        md_lite_ctrl_feed(&g_lite, uart_getc(uart0));
}

void ctrl_cb_demo(void)
{
    md_lite_ctrl_init(&g_lite, on_speed);
    uint8_t buf[32];
    uint16_t n = md_lite_subscribe(50, buf, sizeof(buf));
    user_send(buf, n);                           /* 设置回调后先订阅 */

    irq_set_exclusive_handler(UART0_IRQ, on_uart_rx);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(uart0, true, false);
    /* 主循环里周期性 user_send(md_lite_ctrl(...)) */
}
```

## 四、集成步骤

1. 本目录的 `CMakeLists.txt` 已把 `mdc_lib.c` / `mdc_lite.c` / `mdc_lite_ctrl.c` 一起打包为 `mdc_lib` 静态库；在你的 pico-sdk 工程 `add_subdirectory(point_to_this_dir)` 后 `target_link_libraries(<app> PRIVATE mdc_lib)` 即可。
2. 只需发送 → `#include "mdc_lite.h"`；需回读转速 → `#include "mdc_lite_ctrl.h"`（后者已包含前者）。
3. 按上面「串口接入示例」用 `uart_init`/`uart_write_blocking`/接收中断喂 `md_lite_ctrl_feed`。
4. 上位机实时控制请先发 `/priority 1`（USB 优先）或确保 USART2 优先（`/priority 0`），否则控制帧受仲裁限制。

## 五、与 mdc_lib 的关系

| | mdc_lib（完整） | mdc_lite（极简） |
|---|---|---|
| 关注范围 | 文本指令 + config + SBUS/检测 + 19 条二进制命令 | 只有 0x31/0x40/0x41/0xF0 4 条 |
| 字节布局 / CRC / 帧格式 | 协议规范 v2.1 | **与 mdc_lib 完全一致**（独立实现，仅字节兼容，不共享代码） |
| 实现方式 | 独立实现全套 | `mdc_lite.c` 自带 CRC8/组帧；`mdc_lite_ctrl.c` 自带滑窗找 0xAA + CRC8 校验的流式解析，取 rpm 后回调 |
| 依赖 | 无（纯 C） | 只 include 同族 `mdc_lite.h`，**不依赖 mdc_lib.h** |
| 需要哪个 | 全部功能 | 只有「上位机调参 / 下位机执行」的简单场景 |

需要完整 API（`md_parse_ack`/`md_parse_config` 等）时直接 `#include "mdc_lib.h"`，与本极简层共存、互不冲突。

## 六、校验状态

本平台 `mdc_lite.c` / `mdc_lite_ctrl.c` 为**独立实现**（不依赖 `mdc_lib.h`），已通过本机
`gcc -std=c99 -Wall -Wextra -pedantic -c` 零警告校验，并经一轮字节向量断言（CRC 0x15/0xF4、
`subscribe(50)` 帧、`ctrl(100,-200,0,300)` DATA、56B/72B 0xF0 回调 rpm 四值）全部通过。
与 stm32/hal、esp32/esp-idf 的 `mdc_lite.*` 代码逐字节一致。
