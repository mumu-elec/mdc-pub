# RP2040（arduino-pico）单片机例程（mdc_lite）

> 平台：Raspberry Pi Pico 等 RP2040 板（arduino-pico core）｜ 串口：Serial1（`setTX(4)/setRX(5)`）接控制板 **RC 接口（USART2）**
> 使用前请在控制板上执行 `/uart2 115200 0 uart` 并共地。

本平台目前提供以下「极简」功能级例程（各子目录自带库与 `.ino`）：

## 目录导览

| 子目录 | 类别 | 内容 | 建议 |
|--------|------|------|------|
| [`极简控制/`](极简控制/) | 极简 | **mdc_lite** send-only：只打包并发送 0x31 控制帧 | 只发不收，最小程序 |
| [`控制+回调/`](控制+回调/) | 控制+回调 | **mdc_lite_ctrl**：发控制帧 + 流式接收 0xF0 并把 rpm 回调 | 需回读实时转速时用 |

## 最小上手（极简控制）

`极简控制/mdc_lite_send.ino` 就直接用 `md_lite_ctrl(400,0,0,0, frame, sizeof(frame))` 发一帧控制，串口由你实现。

## 库来源与更新

- 极简例程用 **mdc_lite / mdc_lite_ctrl**（独立实现（不依赖 mdc_lib））。
- 库源文件：`mdc_lib/rp2040/arduino/`。更新时用该目录同名文件覆盖各子目录即可。
- 简要文档：`mdc_lib/rp2040/arduino/README.md`（mdc_lib）、`mdc_lib/rp2040/arduino/mdc_lite_README.md`（mdc_lite）。

> 极简库规范见 [`mdc_lib/LITE.md`](../../../LITE.md)。
