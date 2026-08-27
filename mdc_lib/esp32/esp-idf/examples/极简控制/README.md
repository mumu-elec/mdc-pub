# 极简控制（ESP32 ESP-IDF，send-only）

用 `mdc_lite`（极简调用库）**只打包并发送控制帧**，不解析任何回包。演示 `md_lite_ctrl` / `md_lite_stop` / `md_lite_subscribe` / `md_lite_unsubscribe`。

## 文件

| 文件 | 说明 |
|------|------|
| `main.c` | 最小主程序：订阅 + 主循环每 50ms 发一帧 0x31（`user_send` 为编译期桩，替换为 `uart_write_bytes`） |

## 库依赖

- `mdc_lite.h/.c`、`mdc_lib.h/.c`（来自 `../../../../../../../../esp32/esp-idf/`）
- 组件方式：把 `mdc_lib/esp32/esp-idf` 放到工程 `components/mdc_lib/`（CMakeLists 已含 `mdc_lite.c`），`REQUIRES mdc_lib` 即可；也可直接把上述 `.c` 加入 `target_sources`。

## 接线（示意）

ESP32 `UART1_TX → RC RX`；`UART1_RX ← RC TX`；`GND` 共地。
控制板先 `/uart2 115200 0 uart`（USB 串口 2000000-8N1），默认 USART2 优先（`/priority 0`），0x31 控制帧天然生效。

## 编译验证

```bash
gcc -std=c99 -Wall -Wextra -c main.c -I../../../../../../../../esp32/esp-idf
```

> 若需回读 0xF0 四通道 rpm，请用上一级 [`../控制+回调/`](../控制+回调/)。
