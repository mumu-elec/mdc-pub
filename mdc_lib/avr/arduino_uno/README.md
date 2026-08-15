# mdc_lib — AVR（Arduino UNO）接入文档

> 平台：Arduino UNO（ATmega328P，2KB SRAM）｜ 文件：`mdc_lib.h` + `mdc_lib.cpp` + `README.md`
> 协议依据：[`../例程/common/协议规范.md`](../../../例程/common/协议规范.md)（布局 v2.1，config_t = 231B，固件 SW_MAJOR=2）
> API 规范：[`../API.md`](../../API.md)（唯一依据，同一套 `md_*` 签名）

## 一、功能

- **纯 C++ 打包/解析库**：不 include `Arduino.h` / AVR 头文件，与框架无关（本机 g++ `-std=c++17 -Wall -Wextra` 可直接编译校验）。
- **打包**：`md_bin_*`（15 个二进制命令，返回整帧字节）、`md_text_*`（文本指令，返回含 `\n` 的行）。
- **解析**：`md_parse_*`（ACK / STATUS / DETECT / SBUS / config）+ 流式解析器 `md_parser_feed`（自动找 `0xAA` 同步 + CRC 校验）。
- **不碰串口**：串口收发由用户实现 —— 拿库返回的字节 `rc.write(buf, n)` 发送；收到的字节喂给 `md_parser_feed`。

## 二、文件说明

| 文件 | 说明 |
|------|------|
| `mdc_lib.h` | 常量、结构体、函数声明（含中文注释） |
| `mdc_lib.cpp` | 全量实现（纯 C++，仅依赖标准 `<stdint.h>` / `<string.h>`） |

> 四平台（esp32 / rp2040 / avr / esp8266）的 `mdc_lib.h`、`mdc_lib.cpp` **内容完全一致**，只有本 README 的接线/串口示例不同。

## 三、API 速览

### 底层
| 函数 | 说明 |
|------|------|
| `md_crc8(data, len)` | CRC8（多项式 0x07，初值 0）；校验向量 `0x15` / `0xF4` |
| `md_build_frame(cmd, data, len, out, cap)` | 组帧 `[AA][CMD][LEN][DATA][CRC]`，返回字节数 |
| `md_parse_frame(frame, len, &cmd, &payload, &plen)` | 帧级解析（校验 SYNC + CRC），返回 1/0 |
| `md_parser_init(&p)` / `md_parser_feed(&p, byte, &cmd, &payload, &plen)` | 流式解析器（`MD_PARSER_BUF` 默认 256，可裁剪） |

### 文本指令（返回含 `\n` 的文本行）
`md_text_build(cmd, args, out, cap)` 通用构造；便捷封装：`md_text_version / help / status / check / detect / save / load / reset / enczero(ch) / mode(ch, mode)`。其余指令（speedctrl / posctrl / cpr / inv / einv / posangle / filter / uart2 / priority / timeout / smap / rmap / dmap / sbusparam / sbusrange）用 `md_text_build` 构造，如：

```cpp
md_text_build("/speedctrl", "1 0.5 0.02 0.01 500 800 10 0", out, cap);
md_text_build("/uart2",     "115200 0 uart", out, cap);
```

### 二进制命令（15 个，返回整帧字节）
`md_bin_ping / read_param / write_param(cfg) / write_field / save / load / factory_reset / motor_raw(ch,dir,pwm) / motor_ctrl(t0,t1,t2,t3) / subscribe(ms) / unsubscribe / debug_sbus / debug_speed / enter_bl / reboot`，签名统一为 `uint16_t md_xxx(..., uint8_t* out, uint16_t cap)`。

### 解析（5 个）
`md_parse_ack(payload, len, &ack)`、`md_parse_status(payload, len, &st)`（56B/72B 自动兼容）、`md_parse_detect(payload, len, &dt)`、`md_parse_sbus(payload, len, ch[16])`、`md_parse_config(raw, len, &cfg)` + `md_pack_config(&cfg, out, cap)`（往返无损）。

## 四、串口接入示例（UNO → SoftwareSerial）

UNO 硬件串口（0/1）一般被 USB 占用，推荐用 **SoftwareSerial** 接驱动器 USART2（RC 口）。驱动器需**先**通过任意终端配置为 UART 模式并共地：

```
/uart2 9600 0 uart
```

> ⚠️ SoftwareSerial 高速不可靠，示例用 **9600**（需先按上面把驱动器配成 9600；若用 57600/115200 请用硬件串口或换更高性能板）。

```cpp
/* MotorDriver_UNO.ino —— 串口收发由用户实现，库只负责打包/解析 */
#include "mdc_lib.h"
#include <SoftwareSerial.h>

SoftwareSerial rc(10, 11);        /* RX=10 接驱动器 TX，TX=11 接驱动器 RX；务必共地 */

static md_parser_t g_parser;      /* 全局，避免占栈（UNO SRAM 仅 2KB） */

void setup() {
    Serial.begin(9600);           /* USB 调试口 */
    rc.begin(9600);               /* 与驱动器 USART2 同波特率 */
    md_parser_init(&g_parser);
}

void loop() {
    /* ① 发送：打包 -> 串口（实时控制帧按 50ms 周期连续发） */
    static uint32_t last = 0;
    if (millis() - last >= 50) {
        last = millis();
        uint8_t frame[32];
        uint16_t n = md_bin_motor_ctrl(100, -200, 0, 300, frame, sizeof(frame));
        rc.write(frame, n);       /* 用户实现发送 */
    }

    /* ② 接收：串口字节 -> 流式解析 */
    while (rc.available() > 0) {
        uint8_t b = (uint8_t)rc.read();
        uint8_t cmd; const uint8_t* payload; uint16_t plen;
        if (md_parser_feed(&g_parser, b, &cmd, &payload, &plen) == 1) {
            if (cmd == 0xF0) {                            /* STATUS_REPORT */
                md_status_t st;
                if (md_parse_status(payload, plen, &st) == 1) {
                    Serial.print("enc0="); Serial.print(st.enc[0]);
                    Serial.print(" rpm0="); Serial.println(st.rpm[0]);
                }
            }
            /* ACK 帧：md_parse_ack(payload, plen, &ack)，
             * 完整帧模式自动提取 ack.cmd / ack.err */
        }
    }
}
```

### UNO 内存优化（重要）

UNO 只有 2KB SRAM，注意：

- 解析器 `md_parser_t`（默认 256B）与 `md_config_t`（约 260B）**必须声明为全局/static**，不要放函数栈里。
- 不解析 READ_PARAM 应答时，可裁剪缓冲：编译选项 `-DMD_PARSER_BUF=64`（或 128）。注意：**小于 235 时 231B 的 config 应答帧无法完整解析**（流式解析器会自动丢弃）。
- 不使用 config 全字段函数时，可 `-DMD_ENABLE_CONFIG=0` 编译掉 `md_config_t` / `md_parse_config` / `md_pack_config` / `md_bin_write_param`，省约 300B RAM 与大量 Flash（`md_bin_read_param` 仍可用）。
- 实时控制场景推荐直接用 `md_bin_motor_ctrl` 帧（20B），少用文本指令。

## 五、集成步骤

1. 把 `mdc_lib.h`、`mdc_lib.cpp` 复制到 sketch 目录（与 `.ino` 同目录）。
2. 在 `.ino` 开头 `#include "mdc_lib.h"`。
3. 发送：`uint16_t n = md_bin_xxx(..., buf, sizeof(buf)); rc.write(buf, n);`
4. 接收：loop/中断里逐字节 `md_parser_feed(&g_parser, b, &cmd, &payload, &plen)`，返回 1 即收到一帧（`payload` 在下次 feed 前有效）。

## 六、常见问题

| 现象 | 处理 |
|------|------|
| 发控制帧电机不动 | 检查 `/priority`：USB 上位机实时控制先 `/priority 1`；遥控/USART2 主控保持 0 |
| 连不上/乱码 | SoftwareSerial 高速不可靠，把双方都配成 9600；确认共地 |
| 编译内存不足 | 见上文「UNO 内存优化」：裁剪 `MD_PARSER_BUF`、`-DMD_ENABLE_CONFIG=0` |
| 文本指令无响应 | 确认带 `\n`（库已自动加） |
| 控制帧时灵时不灵 | 协议识别（/detect）期间控制帧被拒绝，等待识别完成 |
| 文本回显与二进制混流 | 文本行会被解析器当作噪声丢弃（0xAA 前字节忽略），属正常 |
| CRC 校验失败 | CRC8 范围 = CMD+LEN+DATA（不含 SYNC），多项式 0x07 初值 0 |
| 版本不匹配 | 固件 SW_MAJOR 必须与上位机协议版本 D 一致（当前均为 2） |
| 实时控制被打断 | `/timeout` 超时保护：超过设定时间未收到控制帧，电机输出归零 |
