# mdc_lib — ESP8266（Arduino）接入文档

> 平台：ESP8266（Arduino core）｜ 文件：`mdc_lib.h` + `mdc_lib.cpp` + `README.md`
> 协议依据：[`../../协议规范.md`](../../协议规范.md)（布局 v2.x，config_t = 248B，固件 SW_MAJOR=2）
> API 规范：[`../API.md`](../../API.md)（唯一依据，同一套 `md_*` 签名）

## 一、功能

- **纯 C++ 打包/解析库**：不 include `Arduino.h` / ESP 头文件，与框架无关（本机 g++ `-std=c++17 -Wall -Wextra` 可直接编译校验）。
- **打包**：`md_bin_*`（16 个二进制命令，返回整帧字节）、`md_text_*`（文本指令，返回含 `\n` 的行）。
- **解析**：`md_parse_*`（ACK / STATUS / DETECT / SBUS / config）+ 流式解析器 `md_parser_feed`（自动找 `0xAA` 同步 + CRC 校验）。
- **不碰串口**：串口收发由用户实现 —— 拿库返回的字节 `Serial.write(buf, n)` 发送；收到的字节喂给 `md_parser_feed`。

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

### 二进制命令（16 个，返回整帧字节）
`md_bin_ping / read_param / write_param(cfg) / write_field / save / load / factory_reset / motor_raw(ch,dir,pwm) / motor_ctrl(t0,t1,t2,t3) / motor_jog(ch,rpm) / subscribe(ms) / unsubscribe / debug_sbus / debug_speed / enter_bl / reboot`，签名统一为 `uint16_t md_xxx(..., uint8_t* out, uint16_t cap)`。

### 解析（5 个）
`md_parse_ack(payload, len, &ack)`、`md_parse_status(payload, len, &st)`（56B/72B 自动兼容）、`md_parse_detect(payload, len, &dt)`、`md_parse_sbus(payload, len, ch[16])`、`md_parse_config(raw, len, &cfg)` + `md_pack_config(&cfg, out, cap)`（往返无损）。

## 四、串口接入示例（ESP8266 → Serial）

驱动器 USART2（RC 口）需**先**通过任意终端配置为 UART 模式并共地：

```
/uart2 115200 0 uart
```

```cpp
/* MotorDriver_ESP8266.ino —— 串口收发由用户实现，库只负责打包/解析 */
#include "mdc_lib.h"

static md_parser_t g_parser;              /* 流式解析器（全局，避免占栈） */

void setup() {
    Serial.begin(115200);                 /* UART0：TX=GPIO1 / RX=GPIO3 */

    /* 若 GPIO1/3 与 boot 日志/烧录冲突，可换到 GPIO15(TX)/GPIO13(RX)：
     * Serial.swap();                     // 必须在 begin 之后调用 */
    md_parser_init(&g_parser);
}

void loop() {
    /* ① 发送：打包 -> 串口（实时控制帧按 30ms 周期连续发） */
    static uint32_t last = 0;
    if (millis() - last >= 30) {
        last = millis();
        uint8_t frame[32];
        uint16_t n = md_bin_motor_ctrl(100, -200, 0, 300, frame, sizeof(frame));
        Serial.write(frame, n);           /* 用户实现发送 */

        /* 文本指令示例（带 \n，可省略参数做读取） */
        char line[32];
        n = md_text_mode(1, "speed", line, sizeof(line));   /* "/mode 1 speed\n" */
        Serial.write((const uint8_t*)line, n);
    }

    /* ② 接收：串口字节 -> 流式解析 */
    while (Serial.available() > 0) {
        uint8_t b = (uint8_t)Serial.read();
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

> 注意：上电瞬间 GPIO1/3 会输出 boot 日志（文本），解析器会将其当作噪声丢弃，不影响后续二进制帧解析。

## 五、集成步骤

1. 把 `mdc_lib.h`、`mdc_lib.cpp` 复制到 sketch 目录（与 `.ino` 同目录）。
2. 在 `.ino` 开头 `#include "mdc_lib.h"`。
3. 发送：`uint16_t n = md_bin_xxx(..., buf, sizeof(buf)); Serial.write(buf, n);`
4. 接收：loop/中断里逐字节 `md_parser_feed(&g_parser, b, &cmd, &payload, &plen)`，返回 1 即收到一帧（`payload` 在下次 feed 前有效）。

## 六、常见问题

| 现象 | 处理 |
|------|------|
| 发控制帧电机不动 | 检查 `/priority`：USB 上位机实时控制先 `/priority 1`；遥控/USART2 主控保持 0 |
| 连不上/无响应 | 确认波特率、共地；USART2 需先 `/uart2 <baud> 0 uart` |
| 引脚冲突 | 上电 boot 日志/烧录与 GPIO1/3 冲突时，`Serial.swap()` 换到 GPIO15/13 |
| 文本指令无响应 | 确认带 `\n`（库已自动加） |
| 控制帧时灵时不灵 | 协议识别（/detect）期间控制帧被拒绝，等待识别完成 |
| 文本回显与二进制混流 | 文本行会被解析器当作噪声丢弃（0xAA 前字节忽略），属正常 |
| CRC 校验失败 | CRC8 范围 = CMD+LEN+DATA（不含 SYNC），多项式 0x07 初值 0 |
| 版本不匹配 | 固件 SW_MAJOR 必须与上位机协议版本 D 一致（当前均为 2） |
| 实时控制被打断 | `/timeout` 超时保护：超过设定时间未收到控制帧，电机输出归零 |
