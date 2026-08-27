# mdc_lite — ESP32（Arduino）极简调用库接入文档

> 平台：ESP32（Arduino core）｜ 文件：`mdc_lite.h` + `mdc_lite.cpp`（send-only）、`mdc_lite_ctrl.h` + `mdc_lite_ctrl.cpp`（调用+回调）
> 规范依据：[`../LITE.md`](../LITE.md)（mdc_lite 极简 LITE API，唯一依据）
> 底层复用：[`mdc_lib.h`/`mdc_lib.cpp`](./mdc_lib.h)（API.md 打包/解析原语，字节布局不变）

## 一、功能

**核心思想：上位机调参、下位机执行。** 你只需告诉下位机"发什么控制量"，并从下位机拿回实时转速。

mdc_lite 是完整版 `mdc_lib` 之上的一层**极简封装**，只管这 4 条二进制命令中的极简场景：

| CMD | 名称 | 用途 |
|:---:|------|------|
| `0x31` | MOTOR_CTRL | 发送四通道控制目标（下位机执行） |
| `0x40` | SUBSCRIBE | 开启状态周期上报（收速度的前提） |
| `0x41` | UNSUBSCRIBE | 关闭状态上报（善后） |
| `0xF0` | STATUS_REPORT | 下位机主动上报；取出 `rpm[4]` 做速度回调 |

- **只管调用（send-only）`mdc_lite.*`**：只打包/给出要发送的控制帧，不做任何接收解析。
- **调用+回调接收 `mdc_lite_ctrl.*`**：在 send-only 基础上，**流式接收** `0xF0` 并把四通道 rpm 回调给用户。
- **不碰串口、不碰 config_t/SBUS/文本指令**：库只返回要发送的字节（或写入输出缓冲）并解析喂入的字节；串口收发仍由用户实现。
- **纯 C++**：不 include 任何 Arduino/ESP 头文件，本机 g++ `-std=c++17 -Wall -Wextra` 可直接编译校验。

> 复用约定：本目录 `mdc_lite.h`/`mdc_lite.cpp`/`mdc_lite_ctrl.h`/`mdc_lite_ctrl.cpp` 与 rp2040 / avr / esp8266 **内容完全一致**，仅本 README 的接线/串口示例不同。

## 二、API 速览

### send-only（`mdc_lite.h`，都返回「要发送的字节数」，cap 不足返回 0）

| 函数 | CMD | 说明 |
|------|:---:|------|
| `md_lite_ctrl(m0,m1,m2,m3, out, cap)` | 0x31 | 四通道目标值（int32 LE）。含义随通道模式：open=PWM(±1000)、speed=RPM、pos=0.1° |
| `md_lite_stop(out, cap)` | 0x31 | 便捷：`ctrl(0,0,0,0)` 全零帧，用于退出/急停 |
| `md_lite_subscribe(interval_ms, out, cap)` | 0x40 | 开启状态上报；interval_ms 建议 ≥20（固件钳位） |
| `md_lite_unsubscribe(out, cap)` | 0x41 | 关闭状态上报 |

> 发送示例：`uint16_t n = md_lite_ctrl(100, -200, 0, 300, frame, sizeof(frame));` → `AA 31 10 <16B DATA> <CRC>`（20B）

### 调用+回调（`mdc_lite_ctrl.h`，发送侧同名同语义 + 新增回调）

| 函数 | 说明 |
|------|------|
| `md_lite_ctrl_init(&ctrl, on_speed)` | 注册速度回调并初始化流式解析器 |
| `md_lite_ctrl_feed(&ctrl, byte)` | 逐字节喂入；`0xF0` 完整帧到达时回调 `on_speed(rpm[4])` |

```cpp
typedef void (*md_lite_on_speed_t)(const int32_t rpm[4]);   /* rpm 为四通道实时转速 */
```

```cpp
void on_speed(const int32_t rpm[4]) {
    Serial.printf("rpm=%ld,%ld,%ld,%ld\r\n",
                  (long)rpm[0],(long)rpm[1],(long)rpm[2],(long)rpm[3]);
}
```

## 三、串口接入示例（ESP32 → Serial2）

驱动器 USART2（RC 口）需**先**通过任意终端配置为 UART 模式并共地：`/uart2 115200 0 uart`。

```cpp
/* MotorDriver_ESP32_lite.ino —— 串口收发由用户实现，库只负责打包/回调 */
#include "mdc_lite_ctrl.h"          /* 同时获得 send-only + 回调 */

static md_lite_ctrl_t g_ctrl;       /* 极简控制器（内含流式解析器，全局/static 避免占栈） */

void setup() {
    Serial.begin(115200);                           /* USB 调试口 */

    /* 接驱动器 USART2。ESP32 经典款 Serial2 默认 RX=16 / TX=17，
     * 可用 begin 的第 3/4 参显式指定引脚（按你的开发板改） */
    Serial2.begin(115200, SERIAL_8N1, 16, 17);

    md_lite_ctrl_init(&g_ctrl, on_speed);           /* 注册速度回调 */

    /* 先订阅状态上报（收速度的前提） */
    uint8_t f[16];
    uint16_t n = md_lite_subscribe(50, f, sizeof(f));
    Serial2.write(f, n);
}

void loop() {
    /* ① 发送：打包 -> 串口（实时控制帧按 30/50/100ms 周期连续发，避免 /timeout 归零） */
    static uint32_t last = 0;
    if (millis() - last >= 50) {
        last = millis();
        uint8_t frame[32];
        uint16_t n = md_lite_ctrl(100, -200, 0, 300, frame, sizeof(frame));
        Serial2.write(frame, n);
    }

    /* ② 接收：串口字节逐字节喂给回调接收器（0xF0 到达时自动回调 on_speed） */
    while (Serial2.available() > 0) {
        uint8_t b = (uint8_t)Serial2.read();
        md_lite_ctrl_feed(&g_ctrl, b);
    }
}
```

> 你只需发送与控制相关；若要善后，退出前 `md_lite_unsubscribe(f, sizeof(f))` 关闭上报即可。

## 四、集成步骤

1. 把 `mdc_lite.h`、`mdc_lite.cpp`、`mdc_lite_ctrl.h`、`mdc_lite_ctrl.cpp`（以及**已有的** `mdc_lib.h`、`mdc_lib.cpp`）复制到 sketch 目录（与 `.ino` 同目录）。
2. 只需发送：`#include "mdc_lite.h"`；需要回读转速：`#include "mdc_lite_ctrl.h"`（自动带上发送侧）。
3. 发送：`uint16_t n = md_lite_xxx(..., buf, sizeof(buf)); Serial2.write(buf, n);`
4. 接收：loop/中断里逐字节 `md_lite_ctrl_feed(&g_ctrl, b)`；`0xF0` 到达时自动回调已注册的 `on_speed(rpm[4])`。

## 五、与 mdc_lib 的关系

- **复用而非重写**：`md_lite_*` 内部直接委托 `mdc_lib` 的 `md_bin_motor_ctrl`、`md_bin_subscribe`、`md_bin_unsubscribe`（发送侧）与 `md_parser_feed`、`md_parse_status`（回调侧），字节布局（LE）、CRC8（0x07 初值 0）、帧格式 `[AA][CMD][LEN][DATA][CRC8]` 完全一致，**不改变协议**。
- **只需发送** → 只引 `mdc_lite`；**需要回读转速** → 引 `mdc_lite_ctrl`（同时获得发送与回调）。
- **不覆盖 mdc_lib 的文本指令 / config_t / SBUS / DETECT / 波特率识别等**：那些仍用完整版 `mdc_lib`。
- 校验向量（与 mdc_lib/LITE.md §6 一致）：`md_lite_ctrl(100,-200,0,300)` DATA=`64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00`；`md_lite_subscribe(50)`=`AA 40 02 32 00 <crc>`；`md_lite_unsubscribe()`=`AA 41 00 <crc>`；`md_lite_stop()`=`AA 31 10 <16B 全零> <crc>`。
