# ESP32 Arduino 例程 — UART2 连 RC 口（mdc_lib 打包/解析 + 双 FreeRTOS 任务）

基于 **ESP32 Arduino** 的 Motor Driver Controller 单片机例程：用 HardwareSerial（UART2，TX=17、RX=16）连接控制板 **RC 接口（USART2）**，USB Serial 交互。两个 FreeRTOS 任务并行处理**控制帧发送**与**状态帧解析**。**协议打包/解析全部由 mdc_lib 完成**，本工程只负责串口收发。

## 功能

- **task_send**：每 **50ms** 用 `md_bin_motor_ctrl` 打包并发送一帧 `0x31 MOTOR_CTRL` 控制帧（满足规范 §3.3「实时控制以 30/50/100ms 间隔连续发送」）。目标值通过 USB Serial 命令实时修改。
- **task_recv**：Serial2 字节逐字节喂 `md_parser_feed()`（自动找 0xAA 同步 + CRC 校验）；收到 `0xF0 STATUS_REPORT` 用 `md_parse_status()` 解析 enc/tgt/rpm 打印到 USB Serial；ACK 帧用 `md_parse_ack()` 打印。
- **USB Serial 命令**：

| 命令 | 动作 |
|------|------|
| `pwm 400` | 开环目标 PWM=400（ch1，需先 `/mode 1 open`） |
| `speed 200` | 速度目标 200RPM（ch1，需先 `/mode 1 speed`） |
| `stop` | 四通道目标清零 |
| `mode 1 speed` | 等价文本指令 `/mode 1 speed`（`md_text_mode` 构造后转发） |
| `/version` 等 | 任意以 `/` 开头的文本指令原样转发（`md_text_build` 构造） |

> 例程不再包含任何自写的 CRC8 / 组帧 / 帧解析代码——这些全部由 mdc_lib 提供。

## 硬件接线（引脚表）

| ESP32 | 控制板（RC 接口 / USART2） | 说明 |
|-------|---------------------------|------|
| GPIO17（TX） | RC 信号（RC RX） | 单片机发 → 控制板收 |
| GPIO16（RX） | RC 信号（RC TX） | 控制板发 → 单片机收 |
| GND | GND | **必须共地** |

文字图：

```
ESP32 DevKit                     Motor Driver Controller
┌─────────────┐                  ┌──────────────────┐
│ GPIO17 TX   ├─────────────────►│ RC(RX)  USART2    │
│ GPIO16 RX   │◄─────────────────┤ RC(TX)            │
│ GND         ├─────────────────►│ GND               │
└─────────────┘                  └──────────────────┘
```

> 接线接控制板 **RC 接口（USART2）**，不是 USB 口。若想换引脚，改 `#define RC_RX_PIN / RC_TX_PIN` 即可（注意避开烧录引脚 GPIO0/GPIO2 等）。

## 控制板预配置

用 USB 串口（波特率 **2000000-8N1**）连接控制板后执行：

```
/uart2 115200 0 uart
```

- 波特率 115200、极性正常、模式 uart（立即生效并应用）。
- 默认控制优先级 **USART2 优先**（`/priority 0`），ESP32 从 RC 口发出的 0x31 控制帧天然生效；PC 端串口工具同时发控制帧时需注意仲裁（规范 §1、§3.4）。
- 需要时可用 `/timeout 0` 关闭超时归零保护。

## 依赖与 mdc_lib

本例程依赖 **mdc_lib**（通用调用库，只做打包/解析，串口收发由本工程实现）：

- `mdc_lib.h` / `mdc_lib.cpp` **已随例程内置（本目录）**，与 `motor_driver_esp32.ino` 同目录；Arduino IDE 打开 `.ino` 即自动编译，无需手动复制或添加源文件。
- 如需更新库版本，用 `../../../../mdc_lib/esp32/arduino/` 下的同名文件覆盖本目录文件即可。
- 代码中 `#include "mdc_lib.h"` 即可使用全部 `md_*` API。

## mdc_lib 调用指南

本例程用到的 mdc_lib API（签名与 `mdc_lib.h` 一致，返回「要发送的字节数」，`cap` 不足返回 0）：

| API | 用途 | 实际调用 |
|-----|------|---------|
| `md_bin_motor_ctrl(t0..t3, out, cap)` | 0x31 四通道控制帧（整帧含 CRC8） | `md_bin_motor_ctrl(g_target[0..3], frame, sizeof(frame))` → 20B |
| `md_bin_subscribe(ms, out, cap)` | 0x40 订阅状态上报 | `md_bin_subscribe(100, frame, sizeof(frame))` → 6B |
| `md_text_build(cmd, args, out, cap)` | 任意文本指令（自动补 `\n`） | `md_text_build("/status", NULL, out, sizeof(out))` |
| `md_text_mode(ch, mode, out, cap)` | 控制模式切换 | `md_text_mode(1, "speed", out, sizeof(out))` → `/mode 1 speed\n` |
| `md_parser_init(&p)` | 初始化流式解析器 | setup 中调用一次 |
| `md_parser_feed(&p, byte, &cmd, &payload, &plen)` | 逐字节喂入，完整帧返回 1 | Serial2 每收到 1 字节调用 |
| `md_parse_status(payload, len, &st)` | 解析 0xF0（56B/72B 自动兼容） | 返回 1 后读 `st.enc[]/st.tgt[]/st.rpm[]` |
| `md_parse_ack(payload, len, &ack)` | 解析 ACK | 返回 1 后读 `ack.cmd / ack.err` |

发送/接收骨架（与例程等价）：

```cpp
uint8_t frame[32];
md_parser_t g_parser;

/* 发送：库打包 → 用户串口写出 */
uint16_t n = md_bin_motor_ctrl(100, -200, 0, 300, frame, sizeof(frame));
Serial2.write(frame, n);

/* 接收：串口字节 → 流式解析 */
while (Serial2.available() > 0) {
    uint8_t b = (uint8_t)Serial2.read();
    uint8_t cmd; const uint8_t* payload; uint16_t plen;
    if (md_parser_feed(&g_parser, b, &cmd, &payload, &plen) == 1) {
        if (cmd == MD_CMD_STATUS_REPORT) {          /* 0xF0 */
            md_status_t st;
            if (md_parse_status(payload, plen, &st) == 1) {
                Serial.printf("enc0=%ld rpm0=%ld\r\n",
                              (long)st.enc[0], (long)st.rpm[0]);
            }
        }
    }
}
```

## 开发环境与编译

1. 安装 [Arduino IDE](https://www.arduino.cc/en/software)。
2. mdc_lib 已随例程内置（本目录），无需复制；Arduino IDE 会自动编译同目录的 `mdc_lib.cpp`。
3. 安装 ESP32 开发板包：**文件 → 首选项 → 附加开发板管理器网址** 填入 `https://dl.espressif.com/dl/package_esp32_index.json`，然后 **工具 → 开发板 → 开发板管理器** 搜索 `esp32` 安装 **esp32 by Espressif Systems**。
4. 板型选择：**工具 → 开发板 → ESP32 Arduino → ESP32 Dev Module**。
5. 端口选择：**工具 → 端口 → 选择 ESP32 的 COM 口**（需先装 CP210x/CH340 驱动，视开发板而定）。
6. 打开 `motor_driver_esp32.ino`，点击「上传」。
7. 打开**串口监视器**，波特率 **115200**，换行符「换行」（Newline）。

## 运行与操作说明

1. 上电后 USB Serial 打印提示信息，ESP32 自动发送 `md_bin_subscribe(100)` 开启状态上报，并开始每 50ms 发送 0x31（初始目标全 0，电机不动）。
2. 输入 `mode 1 speed` → ch1 切速度闭环；再输入 `speed 200` → ch1 以 200RPM 转动。
3. 输入 `pwm 400`（配合 `/mode 1 open`）→ 开环驱动。
4. 控制板每 100ms 推送 0xF0，USB Serial 打印 `[0xF0] enc=... tgt=... rpm=...`。
5. 输入 `stop` 清零目标；任意 `/` 开头文本指令原样转发（如 `/status`）。

## 代码结构

| 函数/任务 | 说明 |
|-----------|------|
| `g_parser` | mdc_lib 流式解析器实例（全局，`MD_PARSER_BUF` 默认 256） |
| `send_bin(buf, n)` | 把 mdc_lib 打包结果交给 Serial2 |
| `task_recv` | FreeRTOS 任务：读 Serial2 → `md_parser_feed` → 0xF0/ACK 解析打印 |
| `task_send` | FreeRTOS 任务：每 50ms `md_bin_motor_ctrl` 打包发送 0x31 |
| `handle_usb_line()` | USB Serial 命令分发（pwm/speed/stop/mode/文本指令） |
| `setup() / loop()` | 初始化（含 `md_bin_subscribe`）与 USB 命令读取 |

## 常见问题

| 现象 | 原因 / 处理 |
|------|------------|
| 上传失败 | 按住开发板 BOOT 键再点上传；检查端口/驱动 |
| RC 口无响应 | 控制板未执行 `/uart2 115200 0 uart`；TX/RX 接反、未共地 |
| 没有 0xF0 打印 | 确认上电时 `md_bin_subscribe(100)` 已执行（收到 `[ACK] cmd=0x40` 即成功）；间隔 ≥20ms；确认收的是二进制帧而非文本 |
| 电机不动 | 目标全 0 时不会动；确认 `/mode` 已配置、优先级仲裁（规范 §3.4） |
| 乱码 | 串口监视器波特率 115200；换行符设「换行」 |
| 任务栈溢出警告 | 增大 `xTaskCreate` 的栈大小参数（4096 → 8192） |
