# 控制+回调（RP2040 C SDK，control + speed callback）

用 `mdc_lite_ctrl`（极简调用库）**既发送控制帧，又流式接收 `0xF0`** 并把四通道 rpm 通过回调交给用户。演示 `md_lite_ctrl_init` / `md_lite_ctrl_feed` / `md_lite_ctrl` / `md_lite_subscribe`。

## 文件

| 文件 | 说明 |
|------|------|
| `main.c` | 最小主程序：注册 `on_speed` 回调 → 订阅 → 主循环发 0x31 + 喂接收字节（`user_send` / `user_rx_poll` 为编译期桩，替换为 pico-sdk 的 UART 收发） |

## 库依赖

- `mdc_lite_ctrl.h/.c`、`mdc_lite.h/.c`、`mdc_lib.h/.c`（来自 `../../../../../../../../rp2040/c-sdk/`）
- 库方式：本目录所在库的 `CMakeLists.txt` 已把三者打包为 `mdc_lib` 静态库；`target_link_libraries(<app> PRIVATE mdc_lib)` 即可。

## 接线（示意）

RP2040 `UART0_TX (GPIO0) → RC RX`；`UART0_RX (GPIO1) ← RC TX`；`GND` 共地。
控制板先 `/uart2 115200 0 uart`，默认 USART2 优先（`/priority 0`），0x31 控制帧天然生效。

## 编译验证

```bash
gcc -std=c99 -Wall -Wextra -c main.c -I../../../../../../../../rp2040/c-sdk
```

> 只需发送、不关心转速反馈时用上一级 [`../极简控制/`](../极简控制/)。
