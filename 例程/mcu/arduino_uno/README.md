# Arduino UNO 例程 — SoftwareSerial 连接 RC 口（mdc_lib 打包/解析 + 数字快捷键菜单）

基于 **Arduino UNO（ATmega328P，16MHz）** 的 Motor Driver Controller 单片机例程：用 SoftwareSerial 连接控制板 **RC 接口（USART2）**，支持文本指令终端交互、0x31 实时控制帧发送与 0xF0 状态上报解析。**协议打包/解析全部由 mdc_lib 完成**，本工程只负责串口收发。

## 功能

- **USB 串口交互终端**：在串口监视器输入任意文本指令（如 `/version`、`/status`、`/mode 1 speed`），由 `md_text_build` 自动补 `\n` 后转发到 RC 口；控制板文本应答按行回显到 USB 串口。
- **数字快捷键菜单**：

| 按键 | 动作 |
|:---:|------|
| `1` | 发 `/version`（`md_text_version`） |
| `2` | 发 `/status`（`md_text_status`） |
| `3` | 开环正转 PWM=400（ch1，`md_bin_motor_ctrl`，持续发送） |
| `4` | 开环反转 PWM=-400（ch1，持续发送） |
| `5` | 停止（ch1~4，结束持续发送） |
| `6` | 速度模式目标 100RPM：`md_text_enczero(1)` + `md_text_mode(1,"speed")` + 0x31 帧 |
| `7` | 发 `/save`（`md_text_save`） |

- **状态上报解析**：上电用 `md_bin_subscribe(100)` 订阅，控制板每 100ms 推送 0xF0 状态帧；RC 口字节逐字节喂 `md_parser_feed()`，收到 0xF0 用 `md_parse_status()` 解析 enc/tgt/rpm 打印到 USB 串口，ACK 帧用 `md_parse_ack()` 打印。
- 快捷键 3/4/6 开启后按 **50ms 周期连续发送** 0x31 控制帧（满足规范 §3.3「实时控制以 30/50/100ms 间隔连续发送」），避免单帧被 `/timeout` 超时归零。

> 例程不再包含任何自写的 CRC8 / 组帧 / 帧解析代码——这些全部由 mdc_lib 提供。

## 硬件接线（引脚表）

| Arduino UNO | 控制板（RC 接口 / USART2） | 说明 |
|-------------|---------------------------|------|
| Pin 11（TX） | RC 信号（RC RX） | 单片机发 → 控制板收 |
| Pin 10（RX） | RC 信号（RC TX） | 控制板发 → 单片机收 |
| GND | GND | **必须共地** |

文字图：

```
Arduino UNO                     Motor Driver Controller
┌──────────┐                    ┌──────────────────┐
│ Pin11 TX ├───────────────────►│ RC(RX)  USART2    │
│ Pin10 RX │◄───────────────────┤ RC(TX)            │
│ GND      ├───────────────────►│ GND               │
└──────────┘                    └──────────────────┘
```

> 注意：接线接的是控制板的 **RC 接口（USART2）**，不是 USB 口。TX/RX 交叉连接，GND 共地。

## 控制板预配置

控制板 USART2 默认为遥控模式（SBUS/ELRS），必须先切到 UART 模式。用 USB 串口工具（波特率 **2000000-8N1**）连接控制板后执行：

```
/uart2 115200 0 uart
```

- 波特率 115200，极性正常，模式 uart（立即生效并应用）。
- 默认控制优先级为 **USART2 优先**（`/priority 0`），因此本例程从 RC 口发出的 0x31 控制帧天然生效；若 PC 端串口工具同时发控制帧，需注意仲裁（规范 §1、§3.4）。
- 若想关闭超时归零保护，可执行 `/timeout 0`（可选）。

## 依赖与 mdc_lib

本例程依赖 **mdc_lib**（通用调用库，只做打包/解析，串口收发由本工程实现）：

- `mdc_lib.h` / `mdc_lib.cpp` **已随例程内置（本目录）**，与 `motor_driver_uno.ino` 同目录；Arduino IDE 打开 `.ino` 即自动编译，无需手动复制或添加源文件。
- 如需更新库版本，用 `../../../mdc_lib/avr/arduino_uno/` 下的同名文件覆盖本目录文件即可。
- 代码中 `#include "mdc_lib.h"` 即可使用全部 `md_*` API。
- 内存说明：UNO 只有 2KB SRAM，本例程使用 mdc_lib **默认配置**（流式解析器缓冲 256B，声明为全局静态，0xF0 状态帧整帧 60B、甚至 READ_PARAM 的 231B 应答都能完整解析）。注意：若想裁剪 `MD_PARSER_BUF`，必须通过编译选项 `-DMD_PARSER_BUF=xxx` **同时作用于 mdc_lib.cpp**（Arduino 会分别编译 sketch 目录下的 .cpp），否则两个编译单元结构体布局不一致会导致解析器越界——不建议初学者修改。

## mdc_lib 调用指南

本例程用到的 mdc_lib API（签名与 `mdc_lib.h` 一致，返回「要发送的字节数」，`cap` 不足返回 0）：

| API | 用途 | 实际调用 |
|-----|------|---------|
| `md_text_version / status / save(out, cap)` | 无参文本指令 | `md_text_version(g_text, sizeof(g_text))` → `/version\n` |
| `md_text_enczero(ch, out, cap)` | 编码器清零 | `md_text_enczero(1, ...)` → `/enczero 1\n` |
| `md_text_mode(ch, mode, out, cap)` | 控制模式切换 | `md_text_mode(1, "speed", ...)` → `/mode 1 speed\n` |
| `md_text_build(cmd, args, out, cap)` | 任意文本指令（自动补 `\n`） | `md_text_build("/speedctrl", "1 0.5 0.02 0.01", ...)` |
| `md_bin_motor_ctrl(t0..t3, out, cap)` | 0x31 四通道控制帧（整帧含 CRC8） | `md_bin_motor_ctrl(400, 0, 0, 0, g_tx, sizeof(g_tx))` → 20B |
| `md_bin_subscribe(ms, out, cap)` | 0x40 订阅状态上报 | `md_bin_subscribe(100, g_tx, sizeof(g_tx))` → 6B |
| `md_parser_init(&p)` | 初始化流式解析器 | setup 中调用一次 |
| `md_parser_feed(&p, byte, &cmd, &payload, &plen)` | 逐字节喂入，完整帧返回 1 | RC 口每收到 1 字节调用 |
| `md_parse_status(payload, len, &st)` | 解析 0xF0（56B/72B 自动兼容） | 返回 1 后读 `st.enc[]/st.tgt[]/st.rpm[]` |
| `md_parse_ack(payload, len, &ack)` | 解析 ACK | 返回 1 后读 `ack.cmd / ack.err` |

发送/接收骨架（与例程等价）：

```cpp
uint8_t  g_tx[32];
md_parser_t g_parser;

/* 发送：库打包 → 用户串口写出 */
uint16_t n = md_bin_motor_ctrl(400, 0, 0, 0, g_tx, sizeof(g_tx));
rcSerial.write(g_tx, n);

/* 接收：串口字节 → 流式解析 */
while (rcSerial.available()) {
    uint8_t b = (uint8_t)rcSerial.read();
    uint8_t cmd; const uint8_t* payload; uint16_t plen;
    if (md_parser_feed(&g_parser, b, &cmd, &payload, &plen) == 1) {
        if (cmd == MD_CMD_STATUS_REPORT) {          /* 0xF0 */
            md_status_t st;
            if (md_parse_status(payload, plen, &st) == 1) {
                Serial.println(st.rpm[0]);          /* 消费：payload 在下次 feed 前有效 */
            }
        }
    }
}
```

## 开发环境与编译

1. 安装 [Arduino IDE](https://www.arduino.cc/en/software)（任意近期版本）。
2. mdc_lib 已随例程内置（本目录），无需复制；Arduino IDE 会自动编译同目录的 `mdc_lib.cpp`。
3. 板型选择：**工具 → 开发板 → Arduino AVR Boards → Arduino Uno**。
4. 端口选择：**工具 → 端口 → 选择 UNO 对应的 COM 口**。
5. 打开 `motor_driver_uno.ino`，点击「上传」（本例程不依赖任何第三方库，仅用内置 SoftwareSerial + mdc_lib）。
6. 打开**串口监视器**，波特率选 **115200**，换行符选「换行」（Newline），即可交互。

## 运行与操作说明

- 上电后串口监视器打印快捷键菜单，并自动发送 `md_bin_subscribe(100)` 订阅状态上报。
- 直接输入文本指令（如 `/version`）回车 → 转发到 RC 口，控制板返回内容以 `<< ` 前缀回显。
- 按数字键 `1`~`7` 触发对应快捷键动作。
- 控制板每 100ms 推送 0xF0，USB 串口打印 `[0xF0] enc=... tgt=... rpm=...`。
- 菜单 3/4/6 会持续发送控制帧，按 `5` 停止；需要改变目标值时重新按对应数字即可。

## 代码结构

| 函数/对象 | 说明 |
|------|------|
| `g_parser` | mdc_lib 流式解析器实例（全局，`MD_PARSER_BUF` 默认 256） |
| `send_motor_ctrl()` | 调 `md_bin_motor_ctrl` 打包 0x31 帧并发送（50ms 周期） |
| `tx_bin() / tx_text_built()` | 把 mdc_lib 打包结果交给 SoftwareSerial |
| `printMenu() / runMenu(n)` | 快捷键菜单打印与分发（1~7） |
| `handleLine(line)` | USB 行输入：单数字=菜单，否则 `md_text_build` 转发 |
| `loop()` | USB 收行转发 + RC 收字节喂 `md_parser_feed`（0xF0/ACK 解析打印 + 文本应答回显）+ 50ms 周期连续控制 |

## 常见问题

| 现象 | 原因 / 处理 |
|------|------------|
| RC 口无响应 | 控制板未执行 `/uart2 115200 0 uart`；或 TX/RX 接反、未共地 |
| 串口监视器出现乱码 | 监视器波特率不是 115200；或换行符设置不对（需「换行」） |
| 电机不动 / 转一下即停 | 控制帧被 `/timeout` 超时归零：例程已 50ms 连续发送；确认菜单 3/4/6 处于持续发送状态；或发 `/timeout 0` 关闭保护 |
| **SoftwareSerial 在 115200 下偶发乱码** | ATmega328P 16MHz 下 SoftwareSerial 靠位翻转软件模拟，115200 已接近其速率上限（中断关闭期间会丢字节）。若回显/0xF0 乱码：① 把例程 `rcSerial.begin(115200)` 改为 `9600`，同时在控制板执行 `/uart2 9600 0 uart`；② 或改用硬件 UART（D0/D1）接 RC 口 |
| 没有 `[0xF0]` 打印 | 确认上电时 `md_bin_subscribe` 已发送（收到 `[ACK] cmd=0x40` 即成功）；间隔需 ≥20ms；确认收的是二进制帧而非文本（文本应答只会按行回显） |
| 编译提示内存不足 | 例程使用 mdc_lib 默认配置（解析缓冲 256B 全局静态），UNO 2KB SRAM 可编译；若自行添加其它库后内存不足，可通过编译选项裁剪 `MD_PARSER_BUF`（必须同时作用于 `mdc_lib.cpp`，见「依赖与 mdc_lib」说明），或改用硬件 UART 例程 |
| 控制帧时灵时不灵 | 控制板处于协议自动识别（/detect）期间会拒绝控制帧，稍等识别完成即可 |
| USB 端口识别不到 | 确认 CH340 驱动已装；控制板 USB 口波特率固定 **2000000-8N1**，与 Arduino 的 115200 是两回事 |
