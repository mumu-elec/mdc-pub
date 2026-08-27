# 极简控制例程 — mdc_lite（只管调用 / send-only）

> **目录：** `例程/mcu/51_mcu/极简控制/`
> **定位：** 只演示「发送控制帧」：用 `mdc_lite` 打包 `0x31 MOTOR_CTRL` 并逐字节发出。**不解析任何回包**——若需回读 0xF0 转速，用 [`../控制+回调/`](../控制+回调/)（`mdc_lite_ctrl`）。
> **用的库：** [`mdc_lib`](../../../../mdc_lib/51/keil/mdc_lib.h)（作底层，已内置）+ [`mdc_lite`](../../../../mdc_lib/51/keil/mdc_lite.h)（极简 send-only）。

## 功能

- **发送侧**：`md_lite_ctrl(m0,m1,m2,m3,out,cap)` 打包一条 `0x31 MOTOR_CTRL`（四通道 int32 LE 目标值，含义随通道控制模式：open=PWM(±1000)、speed=RPM、pos=0.1°）；`md_lite_stop` 是 `ctrl(0,0,0,0)` 的便捷急停；`md_lite_subscribe(ms)`（0x40）与 `md_lite_unsubscribe()`（0x41）控制状态上报。
- 每个函数返回**要发送的整帧字节数**（帧 `[AA][CMD][LEN][DATA][CRC8]`，写入你的 `out` 缓冲），由你调用串口发送。
- 例程不含任何自写的 CRC8 / 组帧代码，全部由 mdc_lib/mdc_lite 提供。

## API 速览

```c
#include "mdc_lite.h"
unsigned int n = md_lite_ctrl(300, 0, -150, 0, tx_buf, sizeof(tx_buf)); /* 0x31 -> 20B */
n = md_lite_stop(tx_buf, sizeof(tx_buf));                 /* 0x31 全零 -> 20B */
n = md_lite_subscribe(50, tx_buf, sizeof(tx_buf));        /* 0x40 -> 6B */
n = md_lite_unsubscribe(tx_buf, sizeof(tx_buf));          /* 0x41 -> 4B */
send_packed(tx_buf, n);                                   /* 用户实现的串口发送 */
```

> **发送侧只有这 4 个函数**；本目录不接收回包，因此没有解析器/回调。`cap` 不足或参数非法返回 `0`。

## 代码结构

- `main.c`：`uart_init()`（9600 波特率）→ 发送 `md_lite_subscribe(50)`（可选，用于开启上报；本例不消费）→ 发送一帧 `md_lite_ctrl(300,0,-150,0)` → 主循环每 50ms 重发同一目标（实时控制）。
- 修改目标值：直接改 `main.c` 里 `md_lite_ctrl(...)` 的参数（或换成变量）。

## 工程文件（本目录已内置）

| 文件 | 说明 |
|------|------|
| `main.c` | 样例主体（用户侧：串口收发 + 调用 mdc_lite） |
| `mdc_lite.c` / `mdc_lite.h` | 极简 send-only 库 |
| `mdc_lib.c` / `mdc_lib.h` | 通用底层库（mdc_lite 依赖它） |

## 编译与运行

1. 按顶层 `README.md` 的 Keil 工程创建步骤，把本目录 `main.c` + `mdc_lite.c` + `mdc_lib.c` 加入工程，Include Paths 指向本目录。
2. 工程级 Define 加 `MD_ENABLE_CONFIG=0`（`mdc_lib.c` 编译掉 config 全字段函数与 231B xdata）。
3. 上电后 51 发送订阅帧（可选）与控制帧，控制板按已配置通道模式执行。

## 与 mdc_lib 的关系

`mdc_lite` 是 `mdc_lib` 的**薄封装**：`md_lite_ctrl` == `md_bin_motor_ctrl`、`md_lite_subscribe` == `md_bin_subscribe`、`md_lite_unsubscribe` == `md_bin_unsubscribe`。字节布局、CRC8（0x07, 初值 0）、帧格式与 mdc_lib 完全一致，只是把关注范围收窄到「发送控制」这 4 条命令，不含文本指令 / config_t / SBUS。
