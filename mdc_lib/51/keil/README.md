# mdc_lib — 51 平台（Keil C51）

> **目录：** `mdc_lib/51/keil/`
> **定位：** 通用调用库 —— 只做**打包要发送的字节**与**解析收到的字节**，串口收发由你自己实现（8051 片上 UART 或模拟串口均可）。
> **协议依据：** [`../../协议规范.md`](../../协议规范.md)（布局 v2.1，config_t=231B）
> **API 规范：** [`../API.md`](../API.md)（同一套 `md_*` 签名，本目录逐函数对应）

---

## 一、功能

- **零硬件依赖**：`mdc_lib.c` 只 include `<stdint.h>` / `<string.h>`，不依赖任何 Keil 外设头文件；只做字节打包与解析。
- **CRC8 / 组帧 / 帧解析**：`md_crc8`、`md_build_frame`、`md_parse_frame`。
- **流式解析器**：`md_parser_t` + `md_parser_init` + `md_parser_feed`，逐字节喂入，自动找 `0xAA` 同步、校验 CRC，可在串口中断里直接调用。
- **文本指令层**：`md_text_build` + 10 个便捷封装（`/version`、`/help`、`/status`、`/check`、`/detect`、`/save`、`/load`、`/reset`、`/enczero`、`/mode`）。
- **二进制命令层**：15 个打包函数（PING ~ REBOOT，见 `mdc_lib.h`）。
- **解析层**：`md_parse_ack` / `md_parse_status`（56B/72B 自动兼容）/ `md_parse_detect` / `md_parse_sbus` / `md_parse_config` ↔ `md_pack_config`。

**针对 8051 的适配：**

| 要求 | 本实现 |
|------|--------|
| C89 兼容 | 声明在块首、无 `//` 注释、无变长数组，Keil C51 直接编译 |
| 注释语言 | 全部英文（避免 Keil 编码兼容问题） |
| 大数组放 xdata | 解析器缓冲在用户声明处加 `xdata`；config 打包缓冲为文件内 `static xdata`（见下） |
| 小内存裁剪 | `MD_ENABLE_CONFIG=0` 编译掉 config 全字段函数（省 231B xdata + 代码）；`MD_PARSER_BUF` 可调小 |

## 二、RAM 占用与裁剪方法（重要）

8051 内部 RAM 很小（SMALL 模型仅 128B），**大数组必须放 xdata**（STC15/STC8 等通常有 1~4KB xdata）。

**默认配置下的 RAM 占用：**

| 占用项 | 大小 | 说明 |
|--------|------|------|
| `md_parser_t` 实例（用户声明） | `MD_PARSER_BUF` + 2B ≈ **258B** | 需用户写 `xdata md_parser_t g_parser;` |
| `s_cfg_tmp`（config 打包缓冲） | **231B** | 库内 `static xdata`，仅 `MD_ENABLE_CONFIG=1` 时存在 |
| 合计（默认） | **≈ 489B xdata** | 256B 解析缓冲 + 231B config 缓冲 + 少量 |

**裁剪方法（按需组合）：**

1. **只做控制不做配置**：`#define MD_ENABLE_CONFIG 0`（在 include 前定义），编译掉 `md_pack_config` / `md_parse_config` / `md_bin_write_param` 及 231B xdata 缓冲 → 省 **231B xdata + 大量代码空间**。
2. **调小解析器缓冲**：`#define MD_PARSER_BUF 64` 等。注意帧总长 = 4 + LEN：
   - STATUS_REPORT 56B → 整帧 60B，需 ≥ 60；
   - STATUS_REPORT 72B → 整帧 76B，需 ≥ 76；
   - SBUS_DATA 32B → 整帧 36B；
   - **READ_PARAM 的 231B config 应答 → 整帧 235B，缓冲 < 235 时该帧无法完整解析**（帧级 `md_parse_frame` 或加大缓冲才能用）。
3. **发送缓冲复用**：`md_bin_motor_ctrl` 等打包函数写入你的发送缓冲（`xdata` 或 `idata` 均可），帧最大 235B（WRITE_PARAM 时）。
4. 若仍需读取配置，可只发 `md_bin_read_param` 并用帧级 `md_parse_frame` + `md_parse_config`，解析器缓冲可保持较小。

**示例（裁剪后最小配置）：**

```c
#define MD_ENABLE_CONFIG 0          /* 省 config 全字段函数与 231B xdata */
#define MD_PARSER_BUF    64         /* 只收 STATUS 56B / SBUS 帧 */
#include "mdc_lib.h"

xdata md_parser_t g_parser;         /* 解析器缓冲必须放 xdata */
xdata uint8_t     g_tx[64];         /* 发送缓冲也建议放 xdata */
```

## 三、串口接入示例（Keil C51）

```c
#include "mdc_lib.h"
#include <reg52.h>

xdata md_parser_t g_parser;         /* 解析器缓冲放 xdata */
xdata uint8_t     g_tx[64];         /* 发送缓冲 */
static uint8_t    g_rx_byte;

/* ① 发送：把打包好的字节交给你的串口发送函数（用户实现） */
void user_send(const uint8_t* buf, uint16_t n)
{
    uint16_t i;
    for (i = 0; i < n; i++) {
        SBUF = buf[i];               /* 以 8051 片上 UART 为例 */
        while (!TI); TI = 0;
    }
}

/* ② 初始化 */
void user_init(void)
{
    md_parser_init(&g_parser);
    /* 串口初始化（波特率/中断使能）按你的硬件配置 */
}

/* ③ 串口接收中断：逐字节喂解析器 */
void uart_isr(void) interrupt 4
{
    uint8_t cmd;
    const uint8_t* payload;
    uint16_t plen;

    if (RI) {
        RI = 0;
        g_rx_byte = SBUF;
        if (md_parser_feed(&g_parser, g_rx_byte, &cmd, &payload, &plen)) {
            if (cmd == MD_CMD_STATUS_REPORT && plen == 56) {   /* 0xF0 状态上报 */
                md_status_t st;
                if (md_parse_status(payload, plen, &st)) {
                    /* 使用 st.enc[0]、st.rpm[0]、st.tgt[0] ... */
                }
            } else if (cmd == MD_CMD_PING && plen == 1) {      /* 0x01 ACK */
                md_ack_t ack;
                if (md_parse_ack(payload, plen, &ack) && ack.err == MD_ERR_OK) {
                    /* PING 成功 */
                }
            }
        }
    }
}

/* ④ 控制示例：实时控制帧（需先 /priority 1） */
void motor_ctrl_demo(void)
{
    uint16_t n = md_bin_motor_ctrl(300, 0, -150, 0, g_tx, sizeof(g_tx));
    user_send(g_tx, n);
}
```

> **注意：** `md_parser_feed` 返回 1 时 `payload` 指向解析器内部缓冲，必须在**下一次 feed 之前**消费（上面示例立即解析）。若要跨中断保存，请 `memcpy` 出来（目标也放 xdata）。

## 四、集成步骤（Keil）

1. 新建 Keil C51 工程（如 STC/AT89 系列），把 `mdc_lib.h` / `mdc_lib.c` 加入工程。
2. 工程选项 → C51 → 代码优化按需（推荐 Level 8 或以上以省 code）；**内存模型**建议 LARGE 或按需（库内大缓冲已显式 xdata，与模型无关）。
3. 按需裁剪（见第二节）：在包含 `mdc_lib.h` 之前 `#define MD_ENABLE_CONFIG 0` / `#define MD_PARSER_BUF 64`。
4. 实现串口收发（见第三节），`#include "mdc_lib.h"` 后调用 `md_*` 函数。

**GCC 语法校验说明**：`xdata` 是 Keil C51 关键字，不是宏。本库源码通过 `MD_51_XDATA` 宏做编译器探测：Keil（`__C51__`）/ SDCC（`__SDCC__`）下展开为真正的 `xdata` 关键字，其它编译器（如本机 gcc）下展开为空，因此**直接校验即可**：

```
gcc -std=c89 -fsyntax-only mdc_lib.c
```

也可用 -D 方案（把 `xdata` 定义为空）达到同样效果：

```
gcc -std=c89 -Dxdata= -fsyntax-only mdc_lib.c
```

Keil 编译时不需要任何额外定义（`xdata` 是内建关键字，`__C51__` 自动识别）。

## 五、常见问题

| 现象 | 原因 / 处理 |
|------|------------|
| 编译报 `SEGMENT TOO LARGE` | 大数组没放 xdata：解析器实例必须写 `xdata md_parser_t g_parser;`；发送缓冲也建议 xdata |
| `md_parser_feed` 收不到帧 | 波特率/接线问题（RC 口需先 `/uart2 <baud> 0 uart`）；确认对方发 `0xAA` 开头二进制帧 |
| 收到帧但 CRC 总失败 | 确认 `md_crc8` 计算范围是 CMD+LEN+DATA（不含 SYNC），与固件一致 |
| 文本指令无响应 | 文本行必须以 `\n` 结尾（本库已自动带） |
| 收不到 READ_PARAM 应答 | 231B 应答整帧 235B，`MD_PARSER_BUF` 需 ≥ 235，或用帧级 `md_parse_frame` |
| RAM 不够 | 按第二节裁剪：`MD_ENABLE_CONFIG=0` + 调小 `MD_PARSER_BUF` |
| Keil 报未定义 `memcpy/memmove` | 确认已包含 `<string.h>`（本库已包含） |
| 想用极简库 | 用同目录 `mdc_lite.h/.c`（只发送）或 `mdc_lite_ctrl.h/.c`（发送+速度回调），见第七节 |
| `md_lite_ctrl_feed` 不回调 | 确认先调用 `md_lite_ctrl_init(cb)`；对方确实发了 `0xF0` 且 CRC 通过（解析器缓冲已内置，覆盖 56B/72B 整帧 60B/76B） |
| 只用发送是否要引 mdc_lite_ctrl | 不必。只发送引 `mdc_lite` 即可；要回读转速才引 `mdc_lite_ctrl`（同时获得发送+回调） |

## 六、校验状态

本平台 `mdc_lib.c` 已通过本机 `gcc -std=c89 -fsyntax-only` 与 `gcc -std=c11 -Wall -Wextra -fsyntax-only` 校验（`xdata` 由 `MD_51_XDATA` 宏桥接，无需额外参数），并与 API.md §8 验证向量（CRC 0x15/0xF4、PING 帧 `AA 01 00 15`、MOTOR_CTRL DATA、config 往返、流式解析器）逐项比对通过。

本平台的极简库 `mdc_lite.c` / `mdc_lite_ctrl.c` 亦通过本机 `gcc -std=c89 -Wall -Wextra -pedantic -fsyntax-only`（零告警）；`test_mdc_lite.c`（host-only 自检）编译运行输出 `ALL OK`，逐项通过 LITE.md §6 一致性用例（见 7.6）。

## 七、mdc_lite 极简调用库（LITE API，极简控制 / 控制+回调）

> **定位：** 在 `mdc_lib` 之上的一个**极简接口**，只服务一个场景——**上位机调参、下位机执行**：
> 你只需告诉下位机要发什么控制量、并从下位机拿回实时转速。**不碰文本指令 / config_t 全字段 / SBUS / 波特率识别等**。
> **独立实现（本平台）：** `mdc_lite.*` / `mdc_lite_ctrl.*` 均为**自包含**实现——自带 CRC8(0x07, 初值0) 与帧格式 `[AA][CMD][LEN][DATA][CRC8]` 的打包/解析；
> **不 `#include "mdc_lib.h"`、不调用 `md_bin_*`**，可与 mdc_lib 混用但互不依赖，字节布局与 mdc_lib 完全一致。

**极简范围（只涉及这 4 条二进制命令）：** `0x31 MOTOR_CTRL`（发送）、`0x40/0x41 SUBSCRIBE/UNSUBSCRIBE`、`0xF0 STATUS_REPORT`（速度回调）。

| 文件 | 形态 | 内容 |
|------|------|------|
| `mdc_lite.h/.c` | **只管调用（send-only）** | 只打包要发送的控制帧（0x31/0x40/0x41），不做任何接收解析 |
| `mdc_lite_ctrl.h/.c` | **调用+回调接收（control + speed callback）** | 在 send-only 基础上，流式接收 `0xF0` 并把四通道转速回调给用户 |

### 7.1 功能

- **发送侧（mdc_lite）**：`md_lite_ctrl`（0x31 四通道目标）、`md_lite_stop`（全零急停）、`md_lite_subscribe`（0x40，周期上报）、`md_lite_unsubscribe`（0x41）。全部返回**要发送的整帧字节**（含 SYNC+CRC8），写入你的发送缓冲；`cap` 不足或参数非法返回 `0`。
- **接收回调（mdc_lite_ctrl）**：`md_lite_ctrl_init(cb)` 注册速度回调并复位**单实例**流式解析器；`md_lite_ctrl_feed(byte)` 逐字节喂入，收到完整且 CRC 通过的 `0xF0 STATUS_REPORT`（56B/72B 自动兼容）时解析出 `rpm[4]` 并调用回调。其他帧 / 噪声忽略，可与其他流量混流。

**针对 8051 的裁剪：**
- 解析器缓冲区与中间状态位于本文件内 `static xdata`（外部 RAM 一个 `MD_LITE_CTRL_BUF` 字节滑窗），不占 8051 内部 RAM；仅支持**单路接收**（一个 UART 链路，8051 常规情形）。

### 7.2 API 速览

```c
/* 只用发送：引 mdc_lite.h */
#include "mdc_lite.h"
uint16_t n = md_lite_ctrl(300, 0, -150, 0, g_tx, sizeof(g_tx));  /* 0x31 -> 20B */
n = md_lite_stop(g_tx, sizeof(g_tx));                            /* 0x31 全零 -> 20B */
n = md_lite_subscribe(50, g_tx, sizeof(g_tx));                   /* 0x40 -> 6B */
n = md_lite_unsubscribe(g_tx, sizeof(g_tx));                     /* 0x41 -> 4B */

/* 发送 + 速度回调：引 mdc_lite_ctrl.h（内含 mdc_lite.h） */
#include "mdc_lite_ctrl.h"
md_lite_ctrl_init(on_speed);              /* 注册回调 + 复位解析器 */
md_lite_ctrl_feed(byte);                  /* UART 中断里逐字节喂入 */
```

### 7.3 串口接入示例（Keil C51）

**（a）只发送（send-only）——最小示例：**

```c
#include "mdc_lite.h"
#include <reg52.h>

xdata uint8_t g_tx[20];                       /* 发送缓冲放 xdata */

void user_send(const uint8_t* buf, uint16_t n)
{
    uint16_t i;
    for (i = 0; i < n; i++) { SBUF = buf[i]; while (!TI); TI = 0; }
}

void demo_send_only(void)
{
    uint16_t n;
    n = md_lite_ctrl(300, 0, -150, 0, g_tx, sizeof(g_tx));  /* 四通道目标（int32 LE） */
    user_send(g_tx, n);
    n = md_lite_stop(g_tx, sizeof(g_tx));                   /* 急停/退出 */
    user_send(g_tx, n);
    n = md_lite_unsubscribe(g_tx, sizeof(g_tx));            /* 关闭上报（善后，可选） */
    user_send(g_tx, n);
}
```

**（b）发送 + 速度回调（mdc_lite_ctrl）——最小示例：**

```c
#include "mdc_lite_ctrl.h"
#include <reg52.h>

xdata uint8_t g_tx[20];

void on_speed(const int32_t rpm[4])           /* 速度回调：四通道转速（int32） */
{
    /* 立即消费 rpm[0..3]；此指针在返回后失效，需保留请自行拷出 */
}

void user_send(const uint8_t* buf, uint16_t n)
{
    uint16_t i;
    for (i = 0; i < n; i++) { SBUF = buf[i]; while (!TI); TI = 0; }
}

void uart_isr(void) interrupt 4                /* UART1 接收中断里逐字节喂入 */
{
    if (RI) { RI = 0; md_lite_ctrl_feed(SBUF); }
    if (TI) { TI = 0; }
}

void demo_ctrl_callback(void)
{
    uint16_t n;
    md_lite_ctrl_init(on_speed);                     /* 注册回调 */
    n = md_lite_subscribe(50, g_tx, sizeof(g_tx));   /* 订阅（收速度的前提，固件钳位 >=20ms） */
    user_send(g_tx, n);
    n = md_lite_ctrl(100, 0, 0, 0, g_tx, sizeof(g_tx));
    user_send(g_tx, n);
}
```

> **要点：** 发送函数返回后即可发出；`md_lite_ctrl_feed` 只在完整 `0xF0` 帧到达时才调用回调。若一个工程只发送，引 `mdc_lite`；若还需回读转速，引 `mdc_lite_ctrl`（同时获得发送 + 回调）。

### 7.4 集成步骤

1. 在 Keil 工程加入 `mdc_lite.h/.c`（仅发送）或再加 `mdc_lite_ctrl.h/.c`（要回读转速）。**`mdc_lite` 为独立实现，无需再一起加 `mdc_lib.h/.c`**。右键 Source Group → Add Existing Files…，并按上文在 Include Paths 加头文件目录。
2. 发送缓冲放 xdata（见 7.3）。`md_lite_ctrl_init` 只需调用一次，在开始 `md_lite_ctrl_feed` 前完成。
3. 实现串口收发（用户侧），用 `md_lite_*` 打包、`md_lite_ctrl_feed` 喂字节。

### 7.5 与 mdc_lib 的关系

- `mdc_lite` / `mdc_lite_ctrl` 为**独立实现**：不再复用 `mdc_lib` 的 `md_bin_*` / `md_parser_*` / `md_parse_status`，而是自带 CRC8、组帧、`0xAA` 滑窗流式解析与 `rpm` 提取。**字节布局、CRC、帧格式与 mdc_lib 完全一致**，可与之混用。
- 差别只在命名（`md_lite_` 前缀）与**关注范围收窄**（只 4 条命令），不含文本指令 / config_t 全字段 / SBUS。

### 7.6 一致性验证

本目录 `test_mdc_lite.c`（**host-only 自检，不是 51 目标代码，不要把该文件加入 Keil 工程**）已在主机 gcc 上验证：

```
gcc -std=c89 -Wall -Wextra -pedantic -o ttest mdc_lite.c mdc_lite_ctrl.c test_mdc_lite.c
./ttest        -> 打印 "ALL OK"，退出码 0
```

覆盖了 LITE.md §6 全部一致性用例：`crc8({0x01,0x00})==0x15`、`crc8("123456789")==0xF4`、`subscribe(50)` 帧 `AA 40 02 32 00 <crc>`、`md_lite_ctrl(100,-200,0,300)` DATA `64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00`、`stop`/`unsubscribe` 帧，以及流式接收：噪声 + 非 `0xF0` 帧不触发回调、`0xF0` 56B/72B 均正确解析出四通道 rpm。