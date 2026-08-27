# ESP32 单片机例程（mdc_lib / mdc_lite）

> 平台：ESP32（Arduino core）｜ 串口：HardwareSerial Serial2(16, 17) 接控制板 **RC 接口（USART2）**
> 使用前请在控制板上执行 `/uart2 115200 0 uart` 并共地。

按**功能级别**分三类，各子目录自带库与 `.ino`：

## 目录导览

| 子目录 | 类别 | 内容 | 建议 |
|--------|------|------|------|
| [`完整/`](完整/) | 完整 | mdc_lib 全套：文本指令 + 二进制命令 + 0xF0/ACK 解析（双 FreeRTOS 任务） | 功能最全，带 USB 命令交互 |
| [`极简控制/`](极简控制/) | 极简 | **mdc_lite** send-only：只打包并发送 0x31 控制帧 | 只发不收，最小程序 |
| [`控制+回调/`](控制+回调/) | 控制+回调 | **mdc_lite_ctrl**：发控制帧 + 流式接收 0xF0 并把 rpm 回调 | 需回读实时转速时用 |

## 最小上手（极简控制）

`极简控制/mdc_lite_send.ino` 就直接用 `md_lite_ctrl(400,0,0,0, frame, sizeof(frame))` 发一帧控制，串口由你实现。

## 库来源与更新

- `完整/` 用 **mdc_lib**（`mdc_lib.h/.cpp`）；`极简控制/`、`控制+回调/` 用 **mdc_lite / mdc_lite_ctrl**（独立实现（不依赖 mdc_lib））。
- 库源文件：`mdc_lib/esp32/arduino/`。更新时用该目录同名文件覆盖各子目录即可。
- 简要文档：`mdc_lib/esp32/arduino/README.md`（mdc_lib）、`mdc_lib/esp32/arduino/mdc_lite_README.md`（mdc_lite）。

> 极简库规范见 [`mdc_lib/LITE.md`](../../../LITE.md)。
