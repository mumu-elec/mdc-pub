# 完整（ESP32 ESP-IDF，可选）

本目录为**可选**的「完整协议」示例。它演示 `mdc_lib` 全部能力（文本指令 + 二进制命令 + config 读写 +
SBUS/检测/波特率识别 + 解析），与新例程的 `极简控制/`、`控制+回调/` 对应 LITE.md §4 的「完整」类别。

未内置工程，避免与本平台 `mdc_lib`（组件）重复维护；用 `mdc_lib` 自行编写即可，要点：

- 把 `mdc_lib/esp32/esp-idf` 放到工程 `components/mdc_lib/`，`REQUIRES mdc_lib`。
- 用 `driver/uart.h` 实现收发；接收用 UART 事件循环 + `md_parser_feed` 逐字节喂。
- 参考同平台 `mdc_lib/esp32/esp-idf/README.md` 的串口接入示例。
- 完整协议的可直接参考例程：`mdc_lib/stm32/hal/examples/完整/`（STM32 HAL，行为一致，仅 UART 调用不同）。
