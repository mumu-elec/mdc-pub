# ESP8266 极简控制例程（mdc_lite send-only）

> 平台：ESP8266（Arduino core）｜ 类别：**极简控制**（只发控制帧，不解析回包）
> 库：`mdc_lite.h` / `mdc_lite.cpp`（send-only，复用同目录 `mdc_lib.h` / `mdc_lib.cpp`）

## 功能

只演示"发送控制帧"：用 mdc_lite 打包 `0x31 MOTOR_CTRL`，Serial（UART0）发送，**不做任何接收解析**。
核心思想——上位机调参、下位机执行。

- `md_lite_ctrl(m0,m1,m2,m3, out, cap)`：0x31 四通道控制帧（整帧含 SYNC+CRC8，20B）。
- `md_lite_subscribe(ms, out, cap)`：0x40 订阅状态上报（可选，善后可取消）。
- `md_lite_stop(out, cap)`：0x31 全零（急停/退出）。
- `md_lite_unsubscribe(out, cap)`：0x41 关闭上报（善后）。

**需要回读实时转速** → 请用同目录「控制+回调」例程（`mdc_lite_ctrl`）。

## 硬件接线

| ESP8266 | 控制板（RC 接口 / USART2） | 说明 |
|---------|---------------------------|------|
| GPIO1（TX） | RC 信号（RC RX） | 单片机发 → 控制板收 |
| GPIO3（RX） | RC 信号（RC TX） | 控制板发 → 单片机收 |
| GND | GND | **必须共地** |

> 若 GPIO1/3 与 boot 日志/烧录冲突，可在 `setup()` 中 `Serial.swap()` 换到 GPIO15(TX)/GPIO13(RX)（必须在 `begin` 之后）。接线接控制板 **RC 接口（USART2）**，不是 USB 口。

## 控制板预配置

用 USB 串口（波特率 **2000000-8N1**）连接控制板后执行：**`/uart2 115200 0 uart`**。
默认控制优先级为 USART2 优先（`/priority 0`），本例程从 RC 口发出的 0x31 控制帧天然生效。

> 说明：按本平台约定 RC 口直接用 Serial（UART0 接 USB 串口芯片）。上电 boot 日志会先出现在 TX 上，
> 控制板会把它当作噪声丢弃（0xAA 前字节忽略），不影响后续二进制帧。

## 依赖与 mdc_lite

本目录内置 `mdc_lib.h/.cpp` 与 `mdc_lite.h/.cpp`，与 `.ino` 同目录；Arduino IDE 打开 `.ino`
自动编译同目录 `.cpp`。串口收发由本工程负责，协议打包由 mdc_lite 完成。

## 编译上传

1. 安装 ESP8266 开发板包（**文件 → 首选项 → 附加开发板管理器网址** 填
   `https://arduino.esp8266.com/stable/package_esp8266com_index.json`，然后安装 **esp8266 by ESP8266 Community**）。
2. 板型选你的板子（如 **NodeMCU 1.0** / **LOLIN(WEMOS) D1 mini**），端口选对应 COM 口。
3. 打开 `mdc_lite_send.ino`，点「上传」；串口监视器设 **115200**、换行符「换行」。

## 运行说明

上电后自动发送 `md_lite_subscribe(100)` 与每 50ms 一帧的 `md_lite_ctrl(400,0,0,0, ...)`。
改动目标值直接改 `loop()` 里 `md_lite_ctrl(...)` 的四个参数即可（ch1 目标 400，开环 PWM 或速度 RPM 需配合控制板 `/mode 1 open|speed`）。
