# ESP8266 — mdc_lib 实现

ESP8266 提供两种实现方式，核心代码同一套 API（见 [../API.md](../API.md)）：

| 实现 | 目录 | 适用 |
|------|------|------|
| Arduino | [arduino/](arduino/README.md) | Arduino IDE / PlatformIO |
| MicroPython | [micropython/](micropython/README.md) | MicroPython 固件 |

## 接线（两种实现相同）

ESP8266 UART → 控制板 RC 接口（USART2）：

```
ESP8266 GPIO1 (TXD) ──▶ 控制板 RC 信号
ESP8266 GPIO3 (RXD) ◀── 控制板 RC 信号
GND   ──────────────── 共地
```

控制板需预先配置 USART2 为 UART 模式（通过 USB 上位机发送）：`/uart2 115200 0 uart`

> ⚠️ ESP8266 只有 1 个可用 UART（UART0，与 USB 串口复用）：使用 Arduino 时建议用软件串口（SoftwareSerial）或直接复用 UART0（调试输出改到软串口）；详见各实现 README。

> 库只做打包/解析，串口收发由你的工程实现，两种方式用法完全一致。
