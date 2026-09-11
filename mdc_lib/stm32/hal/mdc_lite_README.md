# mdc_lite — STM32 HAL 平台（纯 C 极简调用库）

> **目录：** `mdc_lib/stm32/hal/`（与 `mdc_lib.h/.c` 同目录，但本极简层**不依赖**它）
> **定位：** 只做「**告诉下位机发什么控制量、从下位机拿回实时转速**」的**独立极简调用库**。
> **只涉及 4 条二进制命令：** `0x31` 控制、`0x40/0x41` 订阅/退订、`0xF0` 状态上报（速度回调）。
> **规范依据：** [`../LITE.md`](../LITE.md)（极简 API 规范，本目录与其逐项对应）
> **独立实现：** `mdc_lite.*` 自带 CRC8、组帧与流式解析，**不 include "mdc_lib.h"**、不调用
> `md_bin_motor_*`/`md_parser_*` 等任何完整库原语；只用到标准 `<stdint.h>`，帧字节布局与 `mdc_lib` 完全一致。

---

## 一、功能

- **零硬件依赖**：`mdc_lite.c` / `mdc_lite_ctrl.c` 只 include `<stdint.h>`，不 include 任何 HAL 头文件；任何 STM32 系列（F1/F4/H7…）或其它 MCU 都能编译。
- **send-only**（`mdc_lite`）：只打包「要发送的控制帧」交给你发送，**不解析任何回包**。
  - `md_lite_ctrl(m0..m3, out, cap)`：0x31 四通道控制帧（**自实现**组帧，非委托 `md_bin_motor_ctrl`）。
  - `md_lite_stop`：`ctrl(0,0,0,0)` 全零控制帧（急停/退出）。
  - `md_lite_subscribe(ms, out, cap)`：0x40 开启状态上报（收速度的前提）。
  - `md_lite_unsubscribe(out, cap)`：0x41 关闭状态上报（善后）。
- **control + 回调**（`mdc_lite_ctrl`）：在 send-only 基础上新增**流式状态接收器**。
  - `md_lite_ctrl_t` + `md_lite_ctrl_init(c, cb)`：注册速度回调并初始化解析器。
  - `md_lite_ctrl_feed(c, byte)`：逐字节喂入；收到完整且 CRC 通过的 `0xF0`（56B/72B 自动兼容）时提取 `rpm[4]` 并调用回调，其余帧/噪声静默丢弃。
- **不碰串口**：库只返回要发送的字节（或写缓冲）并解析喂入的字节；串口收发由你实现。

## 二、API 速览

```c
/* send-only (mdc_lite.h) —— 各函数返回「写入字节数」，cap 不足返回 0 */
uint16_t md_lite_ctrl(int32_t m0,int32_t m1,int32_t m2,int32_t m3, uint8_t* out, uint16_t cap);
uint16_t md_lite_stop(uint8_t* out, uint16_t cap);
uint16_t md_lite_subscribe(uint16_t interval_ms, uint8_t* out, uint16_t cap);
uint16_t md_lite_unsubscribe(uint8_t* out, uint16_t cap);

/* control + speed callback (mdc_lite_ctrl.h，含 send-only 全部函数) */
typedef void (*md_lite_on_speed_t)(const int32_t rpm[4]);
typedef struct { uint8_t buf[MD_LITE_BUF_SIZE]; uint16_t len; md_lite_on_speed_t on_speed; } md_lite_ctrl_t;
void md_lite_ctrl_init(md_lite_ctrl_t* c, md_lite_on_speed_t cb);
void md_lite_ctrl_feed(md_lite_ctrl_t* c, uint8_t byte);
```

> 验证向量（与 mdc_lib 完全一致）：`md_lite_ctrl(100,-200,0,300)` 的 DATA 段
> `== 64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00`；`md_lite_subscribe(50)` 帧
> `== AA 40 02 32 00 9E`；`md_lite_unsubscribe()` 帧 `== AA 41 00 4E`。

## 三、串口接入示例（STM32 HAL）

**① 只用发送（send-only）：** `examples/mcu/stm32_hal/极简控制/` 目录是配套最小示例。这里给骨架：

```c
#include "mdc_lite.h"
#include "usart.h"                 /* 你自己的 HAL 工程头 */

static void user_send(const uint8_t* buf, uint16_t n)
{
    HAL_UART_Transmit(&huart2, buf, n, 100);   /* 串口由你实现 */
}

/* 主循环：先订阅（如需回读转速），再每 50ms 发一帧 0x31 */
void send_demo(void)
{
    uint8_t  buf[32];
    uint16_t n;

    n = md_lite_subscribe(50, buf, sizeof(buf));
    user_send(buf, n);                          /* AA 40 02 32 00 9E */

    n = md_lite_ctrl(100, -200, 0, 300, buf, sizeof(buf));
    user_send(buf, n);                          /* 0x31 四通道控制帧 */

    n = md_lite_stop(buf, sizeof(buf));         /* 全零控制帧（急停） */
    user_send(buf, n);
}
```

**② 发送 + 速度回调（control + callback）：** 配套最小示例见 `examples/mcu/stm32_hal/控制+回调/`。

```c
#include "mdc_lite_ctrl.h"
#include "usart.h"

static md_lite_ctrl_t g_lite;                /* 解析器 + 回调（放静态区，小内存平台避免占栈） */
static uint8_t        g_rx_byte;             /* 单字节接收缓冲 */

/* 0xF0 速度回调：四通道 rpm 实时值 */
static void on_speed(const int32_t rpm[4])
{
    /* 使用 rpm[0..3]（int32） */
    (void)rpm;
}

/* 接收中断：把每字节喂给接收器，0xF0 到达自动回调 on_speed */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart)
{
    if (huart->Instance == USART2) {
        md_lite_ctrl_feed(&g_lite, g_rx_byte);
        HAL_UART_Receive_IT(&huart2, &g_rx_byte, 1);
    }
}

void ctrl_cb_demo(void)
{
    md_lite_ctrl_init(&g_lite, on_speed);      /* 注册回调 + 初始化解析器 */

    uint8_t buf[32];
    uint16_t n = md_lite_subscribe(50, buf, sizeof(buf));  /* 收速度的前提 */
    /* user_send(buf, n); */
    HAL_UART_Receive_IT(&huart2, &g_rx_byte, 1);           /* 开接收中断 */
    /* 主循环里周期性 user_send(md_lite_ctrl(...)) */
}
```

## 四、集成步骤

1. 把 `mdc_lib.h/.c` 与 `mdc_lite.h/.c`、`mdc_lite_ctrl.h/.c` 复制到你的工程（或加入 Keil/IAR/CMake 源文件列表），并添加头文件路径。
2. 只需发送 → `#include "mdc_lite.h"`；需回读转速 → `#include "mdc_lite_ctrl.h"`（后者已包含前者）。
3. 按上面「串口接入示例」实现发送（`HAL_UART_Transmit`）与接收（单字节中断喂 `md_lite_ctrl_feed`）。
4. 上位机实时控制请先发 `/priority 1`（USB 优先）或确保 USART2 优先（`/priority 0`），否则控制帧受仲裁限制。

## 五、与 mdc_lib 的关系

| | mdc_lib（完整） | mdc_lite（极简） |
|---|---|---|
| 关注范围 | 文本指令 + config 全字段读写 + SBUS/检测/波特率 + 19 条二进制命令 | 只有 0x31/0x40/0x41/0xF0 4 条 |
| 字节布局 / CRC / 帧格式 | 协议规范 v2.1 | **与 mdc_lib 完全一致**（独立实现，仅字节兼容，不共享代码） |
| 实现方式 | 独立实现全套打包/解析 | `mdc_lite.c` 自带 CRC8/组帧；`mdc_lite_ctrl.c` 自带滑窗找 0xAA + CRC8 校验的流式解析，取 rpm 后回调 |
| 依赖 | 无（纯 C） | 只 include 同族 `mdc_lite.h`，**不依赖 mdc_lib.h** |
| 需要哪个 | 全部功能 | 只有「上位机调参 / 下位机执行」的简单场景 |

需要 `md_parse_ack`/`md_parse_config` 等完整 API 时，直接 `#include "mdc_lib.h"` 即可（本目录 `mdc_lib` 与极简层共存，互不冲突）。

## 六、校验状态

本平台 `mdc_lite.c` / `mdc_lite_ctrl.c` 为**独立实现**（不依赖 `mdc_lib.h`），已通过本机
`gcc -std=c99 -Wall -Wextra -pedantic -c` 零警告校验，并经一轮字节向量断言（CRC 0x15/0xF4、
`subscribe(50)` 帧 `AA 40 02 32 00 9E`、`unsubscribe` 帧 `AA 41 00 4E`、
`ctrl(100,-200,0,300)` DATA、56B/72B 0xF0 流式回调 rpm 四值 + 噪声/损坏帧不触发）全部通过。
三个平台（stm32/hal、rp2040/c-sdk、esp32/esp-idf）的 `mdc_lite.*` 代码逐字节一致。
