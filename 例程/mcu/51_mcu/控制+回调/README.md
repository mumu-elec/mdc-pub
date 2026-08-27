# 控制+回调例程 — mdc_lite_ctrl（调用 + 速度回调接收）

> **目录：** `例程/mcu/51_mcu/控制+回调/`
> **定位：** 演示「发控制帧 + 收速度回调」：用 `md_lite_ctrl` 发送 `0x31 MOTOR_CTRL`，用 `md_lite_ctrl` 的接收器流式解析下位机主动推送的 `0xF0 STATUS_REPORT`，把四通道 `rpm[4]` 回调给用户。
> **用的库：** [`mdc_lib`](../../../../mdc_lib/51/keil/mdc_lib.h)（底层，已内置）+ [`mdc_lite`](../../../../mdc_lib/51/keil/mdc_lite.h) + [`mdc_lite_ctrl`](../../../../mdc_lib/51/keil/mdc_lite_ctrl.h)（send-only + 回调接收，内含 mdc_lite）。

## 功能

- **发送侧**：与 `极简控制` 相同——`md_lite_ctrl` / `md_lite_stop` / `md_lite_subscribe` / `md_lite_unsubscribe`。
- **接收回调**：`md_lite_ctrl_init(&parser, on_speed)` 注册速度回调并初始化流式解析器；`md_lite_ctrl_feed(&parser, byte)` 逐字节喂入。收到完整且 CRC 通过的 `0xF0 STATUS_REPORT`（56B/72B 自动兼容）时，解析出 `rpm[0..3]`（四通道转速，int32）并调用 `on_speed`。其他帧 / 噪声忽略，可与文本回显等流量混流。

## API 速览

```c
#include "mdc_lite_ctrl.h"
md_lite_ctrl_init(&g_parser, on_speed);        /* 注册回调 + 初始化解析器 */
md_lite_ctrl_feed(&g_parser, byte);            /* UART 中断里逐字节喂入 */

/* 发送侧（同 mdc_lite） */
unsigned int n = md_lite_subscribe(50, tx_buf, sizeof(tx_buf));
n = md_lite_ctrl(100, 0, 0, 0, tx_buf, sizeof(tx_buf));
```

回调签名（LITE.md §3.1）：

```c
void on_speed(const int32_t rpm[4])   /* rpm[0..3] = 四通道 RPM */
```

## 代码结构

| 对象 | 说明 |
|------|------|
| `g_parser` | `xdata md_parser_t`，流式解析器（`MD_PARSER_BUF` 字节缓冲，**必须放 xdata**） |
| `on_speed()` | 速度回调，`0xF0` 到达时被调用；rpm 指针仅本调用内有效 |
| `uart_isr()` | 中断 4 收字节 → `md_lite_ctrl_feed(&g_parser, SBUF)` |
| `main()` | `md_lite_ctrl_init` → 发送 `md_lite_subscribe(50)`（收速度的前提）→ 主循环每 50ms 发一帧控制 |

## 工程文件（本目录已内置）

| 文件 | 说明 |
|------|------|
| `main.c` | 样例主体（用户侧：串口收发 + 调用 mdc_lite + 回调） |
| `mdc_lite_ctrl.c` / `.h` | 极简「调用 + 回调接收」库 |
| `mdc_lite.c` / `.h` | 极简 send-only 库（mdc_lite_ctrl 内含） |
| `mdc_lib.c` / `.h` | 通用底层库（解析器/CRC/组帧） |

## 编译与运行

1. 按顶层 `README.md` 的 Keil 工程创建步骤，把本目录 `main.c` + `mdc_lite_ctrl.c` + `mdc_lite.c` + `mdc_lib.c` 加入工程，Include Paths 指向本目录。
2. 工程级 Define 加 `MD_ENABLE_CONFIG=0`（省 config 全字段函数与 231B xdata）。解析器缓冲 `MD_PARSER_BUF` 默认 256，能容纳最长 0xF0 帧（72B 帧 = 76B）；要在工程级 Define 里把它调小（如 `MD_PARSER_BUF=96`）以省 xdata 时，**必须同时作用于 mdc_lib.c 与 main.c**（它决定 `md_parser_t` 结构大小，两处不一致会越界）。
3. 上电后 51 发送订阅帧，随后每 50ms 发一帧 0x31 控制；控制板按周期推送 0xF0，UART 中断里 `md_lite_ctrl_feed` 触发 `on_speed`。

## 内存说明（重要）

`md_parser_t` 含 `MD_PARSER_BUF`（本例 96）字节缓冲，**必须放 xdata**（`xdata md_parser_t g_parser;`）。`mdc_lite_ctrl` 库内部的中间态 `md_status_t`（约 73B）已由库放在 `static xdata`，不占 8051 内部 RAM。仅支持**单路接收**（一个 UART 链路，8051 常规情形）。

## 与 mdc_lib 的关系

`mdc_lite_ctrl` 发送侧 = `mdc_lite`（= `mdc_lib` 的薄封装）；接收侧复用 `md_parser_t` / `md_parser_feed` / `md_parse_status`（均在 `mdc_lib`）。字节布局、CRC8、帧格式与 mdc_lib 完全一致。若只需发送，引 `mdc_lite` 即可；要回读转速，引 `mdc_lite_ctrl`（同时获得发送 + 回调）。
