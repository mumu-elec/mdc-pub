# mdc_lib — ESP32 ESP-IDF 平台（纯 C 组件）

> **目录：** `mdc_lib/esp32/esp-idf/`
> **定位：** 通用调用库 —— 只做**打包要发送的字节**与**解析收到的字节**，串口收发由你实现。
> **协议依据：** [`../../协议规范.md`](../../协议规范.md)（布局 v2.x，config_t=248B）
> **API 规范：** [`../API.md`](../API.md)（同一套 `md_*` 签名，本目录逐函数对应）

---

## 一、功能

- **零 ESP-IDF 依赖**：`mdc_lib.c` 只 include `<stdint.h>` / `<string.h>`，不 include 任何 `driver/*.h` / `esp_*.h`，可独立编译、独立测试，也方便移植到其它平台。
- **CRC8 / 组帧 / 帧解析**：`md_crc8`、`md_build_frame`、`md_parse_frame`。
- **流式解析器**：`md_parser_t` + `md_parser_init` + `md_parser_feed`，逐字节喂入，自动找 `0xAA` 同步、校验 CRC。
- **文本指令层**：`md_text_build` + 10 个便捷封装（`/version`、`/help`、`/status`、`/check`、`/detect`、`/save`、`/load`、`/reset`、`/enczero`、`/mode`）。
- **二进制命令层**：16 个打包函数（PING ~ REBOOT，见 `mdc_lib.h`）。
- **解析层**：`md_parse_ack` / `md_parse_status`（56B/72B 自动兼容）/ `md_parse_detect` / `md_parse_sbus` / `md_parse_config` ↔ `md_pack_config`。

## 二、API 速览

```c
uint8_t  md_crc8(const uint8_t* data, uint16_t len);
uint16_t md_build_frame(uint8_t cmd, const uint8_t* data, uint16_t data_len,
                        uint8_t* out, uint16_t cap);
int      md_parse_frame(const uint8_t* frame, uint16_t len,
                        uint8_t* cmd, const uint8_t** payload, uint16_t* payload_len);
void     md_parser_init(md_parser_t* p);
int      md_parser_feed(md_parser_t* p, uint8_t byte,
                        uint8_t* cmd, const uint8_t** payload, uint16_t* payload_len);
uint16_t md_text_build(const char* cmd, const char* args, char* out, uint16_t cap);
uint16_t md_bin_ping(uint8_t* out, uint16_t cap);            /* AA 01 00 15 */
uint16_t md_bin_motor_ctrl(int32_t t0, int32_t t1, int32_t t2, int32_t t3,
                           uint8_t* out, uint16_t cap);
uint16_t md_bin_write_param(const md_config_t* cfg, uint8_t* out, uint16_t cap);
int      md_parse_status(const uint8_t* payload, uint16_t len, md_status_t* out);
int      md_parse_config(const uint8_t* raw, uint16_t len, md_config_t* out);
uint16_t md_pack_config(const md_config_t* cfg, uint8_t* out, uint16_t cap);
```

## 三、串口接入示例（ESP-IDF，UART1 事件循环喂解析器）

```c
#include "mdc_lib.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define MDC_UART UART_NUM_1

static md_parser_t g_parser;
static QueueHandle_t g_uart_queue;

/* ① 发送：打包后交给 uart_write_bytes（用户实现收发） */
static void user_send(const uint8_t* buf, uint16_t n)
{
    uart_write_bytes(MDC_UART, (const char*)buf, n);
}

/* ② 接收：UART 事件循环里批量读出字节，逐字节喂解析器（伪代码风格，可直接复制改造） */
static void uart_event_task(void* arg)
{
    uart_event_t ev;
    uint8_t data[128];
    uint8_t cmd;
    const uint8_t* payload;
    uint16_t plen;
    int i;

    (void)arg;
    for (;;) {
        if (xQueueReceive(g_uart_queue, &ev, portMAX_DELAY)) {
            if (ev.type == UART_DATA) {
                int n = uart_read_bytes(MDC_UART, data, ev.size, pdMS_TO_TICKS(10));
                for (i = 0; i < n; i++) {
                    if (md_parser_feed(&g_parser, data[i], &cmd, &payload, &plen)) {
                        if (cmd == MD_CMD_STATUS_REPORT && plen == 56) {  /* 0xF0 状态上报 */
                            md_status_t st;
                            if (md_parse_status(payload, plen, &st)) {
                                /* 使用 st.enc[0]、st.rpm[0]、st.tgt[0] ... */
                                (void)st;
                            }
                        } else if (cmd == MD_CMD_PING && plen == 1) {     /* 0x01 ACK */
                            md_ack_t ack;
                            if (md_parse_ack(payload, plen, &ack) && ack.err == MD_ERR_OK) {
                                /* PING 成功 */
                            }
                        }
                    }
                }
            }
        }
    }
}

/* ③ 初始化：UART1 @ 2000000-8N1 + 事件循环任务 */
void mdc_uart_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = 2000000,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(MDC_UART, &cfg);
    uart_driver_install(MDC_UART, 1024, 1024, 32, &g_uart_queue, 0);
    /* 按你的硬件配置 TX/RX 引脚，例如：
     * uart_set_pin(MDC_UART, TX_GPIO, RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE); */

    md_parser_init(&g_parser);
    xTaskCreate(uart_event_task, "mdc_uart", 4096, NULL, 10, NULL);
}

/* ④ 控制示例：实时控制帧（需先 /priority 1） */
void motor_ctrl_demo(void)
{
    uint8_t buf[64];
    uint16_t n = md_bin_motor_ctrl(300, 0, -150, 0, buf, sizeof(buf));
    user_send(buf, n);
}
```

> **注意：** `md_parser_feed` 返回 1 时，`payload` 指向解析器内部缓冲，必须在**下一次 feed 之前**消费（上面示例立即解析）。若要跨任务保存，请 `memcpy` 出来。

## 四、集成步骤

1. 把本目录复制到工程 `components/mdc_lib/`（含 `CMakeLists.txt` 组件定义），`idf.py build` 自动识别。
2. 在 `main/CMakeLists.txt` 的 `idf_component_register(... REQUIRES mdc_lib)` 中声明依赖。
3. `#include "mdc_lib.h"`，按上面「串口接入示例」实现 UART 收发与事件循环。
4. 上位机实时控制记得先发 `/priority 1`（USB 优先），否则控制帧受仲裁限制。

## 五、常见问题

| 现象 | 原因 / 处理 |
|------|------------|
| `md_parser_feed` 收不到帧 | 波特率/接线问题（USB 需 2000000-8N1）；确认对方确实发 `0xAA` 开头的二进制帧 |
| 收到帧但 CRC 总失败 | 确认 `md_crc8` 计算范围是 CMD+LEN+DATA（不含 SYNC），与固件一致 |
| 文本指令无响应 | 文本行必须以 `\n` 结尾（本库已自动带） |
| 控制帧发了电机不动 | 检查 `/priority`（USB 控制需 1）、`/timeout` 是否超时归零、协议识别期拒绝控制帧 |
| 事件循环丢帧 | `md_parser_feed` 返回后 payload 立即消费；UART 缓冲区大小按需调大 |

## 六、校验状态

本平台 `mdc_lib.c` 与 stm32/rp2040 版本字节级一致，已通过本机 `gcc -std=c11 -Wall -Wextra -fsyntax-only` 零警告校验，并与 API.md §8 验证向量逐项比对通过。
