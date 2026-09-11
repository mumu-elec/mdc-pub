# mdc_lib — STM32 HAL 平台（纯 C）

> **目录：** `mdc_lib/stm32/hal/`
> **定位：** 通用调用库 —— 只做**打包要发送的字节**与**解析收到的字节**，串口收发由你实现。
> **协议依据：** [`../../协议规范.md`](../../协议规范.md)（布局 v2.x，config_t=248B）
> **API 规范：** [`../API.md`](../API.md)（同一套 `md_*` 签名，本目录逐函数对应）

---

## 一、功能

- **零硬件依赖**：`mdc_lib.c` 不 include 任何 HAL 头文件，只用 `<stdint.h>` 与 `<string.h>`，任何 STM32 系列（F1/F4/H7…）或其它 MCU 都能编译。
- **CRC8 / 组帧 / 帧解析**：`md_crc8`、`md_build_frame`、`md_parse_frame`。
- **流式解析器**：`md_parser_t` + `md_parser_init` + `md_parser_feed`，逐字节喂入，自动找 `0xAA` 同步、校验 CRC，可在接收中断里直接调用。
- **文本指令层**：`md_text_build` 通用构造 + 10 个便捷封装（`/version`、`/help`、`/status`、`/check`、`/detect`、`/save`、`/load`、`/reset`、`/enczero`、`/mode`）。
- **二进制命令层**：16 个打包函数（PING / READ_PARAM / WRITE_PARAM / WRITE_FIELD / SAVE / LOAD / FACTORY_RESET / MOTOR_RAW / MOTOR_CTRL / MOTOR_JOG / SUBSCRIBE / UNSUBSCRIBE / DEBUG_SBUS / DEBUG_SPEED / ENTER_BL / REBOOT）。
- **解析层**：`md_parse_ack` / `md_parse_status`（56B/72B 自动兼容）/ `md_parse_detect` / `md_parse_sbus` / `md_parse_config` ↔ `md_pack_config`（248B 全字段、位域、float，往返无损）。

## 二、API 速览

```c
/* 底层 */
uint8_t  md_crc8(const uint8_t* data, uint16_t len);                    /* crc8({0x01,0x00})=0x15 */
uint16_t md_build_frame(uint8_t cmd, const uint8_t* data, uint16_t data_len,
                        uint8_t* out, uint16_t cap);                    /* 返回写入字节数，cap 不足返 0 */
int      md_parse_frame(const uint8_t* frame, uint16_t len,
                        uint8_t* cmd, const uint8_t** payload, uint16_t* payload_len);
void     md_parser_init(md_parser_t* p);
int      md_parser_feed(md_parser_t* p, uint8_t byte,
                        uint8_t* cmd, const uint8_t** payload, uint16_t* payload_len);

/* 文本（返回不含 NUL 的字节数，含 '\n'；cap 不足返 0） */
uint16_t md_text_build(const char* cmd, const char* args, char* out, uint16_t cap);
uint16_t md_text_mode(uint8_t ch, const char* mode, char* out, uint16_t cap);   /* "/mode 1 speed\n" */

/* 二进制（返回整帧字节数） */
uint16_t md_bin_ping(uint8_t* out, uint16_t cap);                       /* AA 01 00 15 */
uint16_t md_bin_motor_ctrl(int32_t t0, int32_t t1, int32_t t2, int32_t t3,
                           uint8_t* out, uint16_t cap);
uint16_t md_bin_write_param(const md_config_t* cfg, uint8_t* out, uint16_t cap);

/* 解析（成功返 1，失败返 0） */
int md_parse_status(const uint8_t* payload, uint16_t len, md_status_t* out);
int md_parse_config(const uint8_t* raw, uint16_t len, md_config_t* out);
uint16_t md_pack_config(const md_config_t* cfg, uint8_t* out, uint16_t cap);
```

## 三、串口接入示例（STM32 HAL）

```c
#include "mdc_lib.h"
#include "usart.h"                 /* 你自己的 HAL 工程头 */

static md_parser_t  g_parser;      /* 流式解析器状态（可放静态区） */
static uint8_t      g_rx_byte;     /* 单字节接收缓冲 */

/* ① 发送：打包后交给 HAL 串口（用户实现收发） */
static void user_send(const uint8_t* buf, uint16_t n)
{
    HAL_UART_Transmit(&huart1, buf, n, 100);
}

/* ② 初始化：开启单字节接收中断 */
void user_init(void)
{
    md_parser_init(&g_parser);
    HAL_UART_Receive_IT(&huart1, &g_rx_byte, 1);
}

/* ③ 接收中断：喂解析器，拿到完整帧后按 CMD 分发（伪代码风格，可直接复制改造） */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    uint8_t cmd;
    const uint8_t* payload;
    uint16_t plen;

    if (huart->Instance == USART1) {
        if (md_parser_feed(&g_parser, g_rx_byte, &cmd, &payload, &plen)) {
            if (cmd == MD_CMD_STATUS_REPORT && plen == 56) {   /* 0xF0 状态上报 */
                md_status_t st;
                if (md_parse_status(payload, plen, &st)) {
                    /* 使用 st.enc[0]、st.rpm[0]、st.tgt[0] ... */
                    (void)st;
                }
            } else if (cmd == MD_CMD_PING && plen == 1) {      /* 0x01 ACK */
                md_ack_t ack;
                if (md_parse_ack(payload, plen, &ack) && ack.err == MD_ERR_OK) {
                    /* PING 成功 */
                }
            }
        }
        HAL_UART_Receive_IT(&huart1, &g_rx_byte, 1);           /* 重新使能接收 */
    }
}

/* ④ 控制示例：实时控制帧（需先 /priority 1） */
void motor_ctrl_demo(void)
{
    uint8_t buf[64];
    uint16_t n;

    n = md_bin_motor_ctrl(300, 0, -150, 0, buf, sizeof(buf));  /* 0x31 帧 */
    user_send(buf, n);
}
```

> **注意：** `md_parser_feed` 返回 1 时，`payload` 指向解析器内部缓冲，必须在**下一次 feed 之前**消费（如上面示例立即调用 `md_parse_status`）。若要跨中断保存，请 `memcpy` 出来。

## 四、集成步骤

1. 把 `mdc_lib.h` / `mdc_lib.c` 复制到你的工程（或加入 Keil/IAR/CMake 的源文件列表），并添加头文件路径。
2. 确认包含路径后 `#include "mdc_lib.h"`，无需链接任何 HAL 库（库本身零依赖）。
3. 按上面「串口接入示例」实现发送与接收中断。
4. 上位机实时控制记得先发 `/priority 1`（USB 优先），否则控制帧受仲裁限制。

## 五、常见问题

| 现象 | 原因 / 处理 |
|------|------------|
| `md_parser_feed` 收不到帧 | 波特率/接线问题（USB 需 2000000-8N1）；确认串口确实在发 `0xAA` 开头的二进制帧 |
| 收到帧但 CRC 总失败 | 确认 `md_crc8` 计算范围是 CMD+LEN+DATA（不含 SYNC），与固件一致 |
| 文本指令无响应 | 文本行必须以 `\n` 结尾（本库已自动带）；部分终端需 `\r\n` |
| 控制帧发了电机不动 | 检查 `/priority`（USB 控制需 1）、`/timeout` 是否超时归零、协议识别期拒绝控制帧 |
| 栈紧张 | `md_bin_write_param` 内部使用 248B 临时缓冲（栈）；可改为调用 `md_pack_config` 后自行 `md_build_frame`，或用 `MD_ENABLE_CONFIG=0` 编译掉 config 全字段函数 |

## 六、校验状态

本平台 `mdc_lib.c` 已通过本机 `gcc -std=c11 -Wall -Wextra -fsyntax-only` 零警告校验，并与 API.md §8 验证向量（CRC 0x15/0xF4、PING 帧 `AA 01 00 15`、MOTOR_CTRL DATA、config 往返、流式解析器）逐项比对通过。
