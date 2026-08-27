# 极简控制（STM32 HAL，send-only）

用 `mdc_lite`（极简调用库）**只打包并发送控制帧**，不解析任何回包。演示 `md_lite_ctrl` / `md_lite_stop` / `md_lite_subscribe` / `md_lite_unsubscribe`。

## 文件

| 文件 | 说明 |
|------|------|
| `main.c` | 最小主程序：订阅 + 主循环每 50ms 发一帧 0x31（`user_send` 为编译期桩，替换为你的 `HAL_UART_Transmit`） |

## 库依赖

- `mdc_lite.h/.c`、`mdc_lib.h/.c`（来自 `../../../../mdc_lib/stm32/hal/`）
- 集成：把 `mdc_lib.c` + `mdc_lite.c` 加入工程，Include Paths 指向 `../../../../mdc_lib/stm32/hal/`。

## 接线

STM32 `PA2 (USART2_TX) → RC RX`；`PA3 (USART2_RX) ← RC TX`；`GND` 共地。
控制板先 ` /uart2 115200 0 uart`，默认 USART2 优先（`/priority 0`），0x31 控制帧天然生效。

## 编译验证

```bash
gcc -std=c99 -Wall -Wextra -c main.c -I../../../../mdc_lib/stm32/hal
```

> 若需回读 0xF0 四通道 rpm，请用上一级 [`../控制+回调/`](../控制+回调/)。
