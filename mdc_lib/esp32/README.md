# ESP32 — mdc_lib 实现

ESP32 提供三种实现方式，核心代码同一套 API（见 [../API.md](../API.md)）：

| 实现 | 目录 | 适用 |
|------|------|------|
| Arduino | [arduino/](arduino/README.md) | Arduino IDE / PlatformIO，快速上手 |
| MicroPython | [micropython/](micropython/README.md) | MicroPython 固件，脚本化开发 |
| ESP-IDF | [esp-idf/](esp-idf/README.md) | 官方框架，生产级工程 |

## 接线（三种实现相同）

ESP32 UART2 → 控制板 RC 接口（USART2）：

```
ESP32 GPIO17 (TX) ──▶ 控制板 RC 信号
ESP32 GPIO16 (RX) ◀── 控制板 RC 信号
GND  ──────────────── 共地
```

控制板需预先配置 USART2 为 UART 模式（通过 USB 上位机发送）：`/uart2 115200 0 uart`

## 选择建议

- **Arduino**：生态成熟、示例多，适合绝大多数用户
- **MicroPython**：改逻辑快，适合原型验证
- **ESP-IDF**：FreeRTOS 深度集成、性能与内存可控，适合量产

> 库只做打包/解析，串口收发由你的工程实现，三种方式用法完全一致。
