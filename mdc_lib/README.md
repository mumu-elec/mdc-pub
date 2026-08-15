# mdc_lib — Motor Driver Controller 通用调用库

> **定位：** 通用调用库 —— 用函数封装指令，**返回要发送的字节**；并提供**接收数据解析**。串口收发由用户自己实现。
> **协议依据：** [`../例程/common/协议规范.md`](../例程/common/协议规范.md)（布局 v2.1，config_t=231B）
> **API 规范：** [`API.md`](API.md)（所有平台实现的唯一依据，同一套 `md_*` 签名）

---

## 一、设计哲学

```
┌─────────────┐   返回字节      ┌──────────────┐
│  mdc_lib    │ ────────────▶  │  用户的串口   │  Serial.write() /
│  打包/解析   │ ◀────────────  │  (用户实现)   │  HAL_UART_Transmit()
└─────────────┘   收到的字节    └──────────────┘
```

- 库只做两件事：**① 打包**（`md_bin_motor_ctrl(...)` → 一帧字节）、**② 解析**（`md_parse_status(...)` → 结构体）。
- **不碰串口**：USB / UART / 蓝牙 / 网络，任何物理通道都能用。
- **统一 API**：12 个实现同一套 `md_*` 签名，换平台只换库文件，业务代码几乎不用改。

## 二、目录导航（按应用设备分类，6 大类 12 个实现）

| 平台 | 实现 | 目录 |
|------|------|------|
| 宿主（PC/树莓派等） | Python | [python/](python/README.md) |
| 宿主（PC/树莓派等） | C++17 | [cpp/](cpp/README.md) |
| STM32 | HAL（F1/F4 通用） | [stm32/hal/](stm32/hal/README.md) |
| ESP32 | Arduino / MicroPython / ESP-IDF | [esp32/](esp32/README.md) |
| RP2040 | Arduino(arduino-pico) / MicroPython / C SDK | [rp2040/](rp2040/README.md) |
| 51 | Keil C51 | [51/keil/](51/keil/README.md) |
| AVR | Arduino UNO | [avr/arduino_uno/](avr/arduino_uno/README.md) |
| ESP8266 | Arduino / MicroPython | [esp8266/](esp8266/README.md) |

## 三、快速开始（最小示例）

**Python（宿主）：**
```python
from mdc_lib import md_bin_motor_ctrl, md_parse_status, MDParser

ser.write(md_bin_motor_ctrl(100, 0, 0, 0))     # 打包 0x31 控制帧 → 发送
parser = MDParser()
for b in ser.read(64):
    frame = parser.feed(b)                      # 流式解析
    if frame and frame[0] == 0xF0:
        st = md_parse_status(frame[1])
        print(st.rpm)
```

**STM32 HAL：**
```c
#include "mdc_lib.h"

uint8_t buf[64];
uint16_t n = md_bin_motor_ctrl(100, 0, 0, 0, buf, sizeof(buf));
HAL_UART_Transmit(&huart1, buf, n, 100);        // 用户实现串口

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    md_parser_feed(&g_parser, rx_byte, &cmd, &payload, &plen);  // 解析
}
```

**Arduino（ESP32 / UNO / RP2040 / ESP8266）：**
```cpp
#include "mdc_lib.h"
uint8_t buf[64];
uint16_t n = md_bin_motor_ctrl(100, 0, 0, 0, buf, sizeof(buf));
Serial2.write(buf, n);                          // 用户实现串口
```

## 四、API 一览

- **底层**：`md_crc8` / `md_build_frame` / `md_parse_frame` / 流式解析器 `md_parser_feed`
- **文本指令**：`md_text_build(cmd, args)` + 10 个便捷封装（version/help/status/check/detect/save/load/reset/enczero/mode）
- **二进制命令**（15 个）：`md_bin_ping / read_param / write_param / write_field / save / load / factory_reset / motor_raw / motor_ctrl / subscribe / unsubscribe / debug_sbus / debug_speed / enter_bl / reboot`
- **解析**（5 个）：`md_parse_ack / status / detect / sbus / config` + `md_pack_config`

> 完整签名、字节布局、验证向量见 [`API.md`](API.md)。

## 五、与「例程」的关系

- `例程/`：完整可运行的示例程序（含串口收发），适合跑通与学习。
- `mdc_lib/`：可复用的协议库（只有打包/解析），适合集成进自己的工程。
- 二者基于同一份协议规范，API 与例程中的协议代码语义一致。

## 六、验证状态

| 实现 | 校验 |
|------|------|
| python / cpp | 本机 py_compile + g++ 编译 + 字节级自测 |
| micropython ×3 / arduino ×4 / C ×4 | 语法校验 + CRC/帧字节级抽查（与 API.md 验证向量比对） |

> CRC8 校验向量：`crc8([0x01,0x00])=0x15`、`crc8("123456789")=0xF4`；PING 帧 `AA 01 00 15`。
