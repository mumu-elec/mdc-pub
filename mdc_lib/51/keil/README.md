# mdc_lib — 51 平台（Keil C51）

> **目录：** `mdc_lib/51/keil/`
> **定位：** 通用调用库 —— 只做**打包要发送的字节**与**解析收到的字节**，串口收发由你自己实现（8051 片上 UART 或模拟串口均可）。
> **协议依据：** [`../../../例程/common/协议规范.md`](../../../例程/common/协议规范.md)（布局 v2.1，config_t=231B）
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

## 六、校验状态

本平台 `mdc_lib.c` 已通过本机 `gcc -std=c89 -fsyntax-only` 与 `gcc -std=c11 -Wall -Wextra -fsyntax-only` 校验（`xdata` 由 `MD_51_XDATA` 宏桥接，无需额外参数），并与 API.md §8 验证向量（CRC 0x15/0xF4、PING 帧 `AA 01 00 15`、MOTOR_CTRL DATA、config 往返、流式解析器）逐项比对通过。
