# RP2040 — mdc_lib 实现

树莓派 Pico（RP2040）提供三种实现方式，核心代码同一套 API（见 [../API.md](../API.md)）：

| 实现 | 目录 | 适用 |
|------|------|------|
| Arduino（arduino-pico） | [arduino/](arduino/README.md) | Arduino IDE / PlatformIO |
| MicroPython | [micropython/](micropython/README.md) | MicroPython 固件 |
| C SDK（官方 Pico SDK） | [c-sdk/](c-sdk/README.md) | C/C++ SDK，裸机/FreeRTOS |

## 接线（三种实现相同）

Pico UART0 → 控制板 RC 接口（USART2）：

```
Pico GP0 (TX) ──▶ 控制板 RC 信号
Pico GP1 (RX) ◀── 控制板 RC 信号
GND  ──────────── 共地
```

控制板需预先配置 USART2 为 UART 模式（通过 USB 上位机发送）：`/uart2 115200 0 uart`

## 选择建议

- **Arduino**：上手最快，与 ESP32 Arduino 版代码几乎一致
- **MicroPython**：脚本化原型验证
- **C SDK**：官方工具链，适合对时序/内存有要求的场景

> 库只做打包/解析，串口收发由你的工程实现，三种方式用法完全一致。
