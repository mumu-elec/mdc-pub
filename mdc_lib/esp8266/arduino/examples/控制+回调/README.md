# ESP8266 控制+回调例程（mdc_lite_ctrl）

> 平台：ESP8266（Arduino core）｜ 类别：**控制+回调**（发控制帧 + 收速度回调）
> 库：`mdc_lite_ctrl.h` / `mdc_lite_ctrl.cpp`（内部复用 `mdc_lite` 与 `mdc_lib`）

## 功能

在 send-only 基础上增加**流式接收 `0xF0 STATUS_REPORT`**，把四通道 rpm 以回调方式交给用户。
核心思想——上位机调参、下位机执行，并从下位机拿回实时转速。

- `md_lite_ctrl(m0,m1,m2,m3, out, cap)`：0x31 控制帧（发送）。
- `md_lite_subscribe(ms, out, cap)`：先订阅，收速度的前提。
- `md_lite_ctrl_init(&g_ctrl, on_speed)`：注册速度回调 `on_speed(rpm[4])`。
- `md_lite_ctrl_feed(&g_ctrl, byte)`：逐字节喂入；`0xF0` 完整帧（56B/72B 自动兼容）到达时自动回调。

回调签名：`typedef void (*md_lite_on_speed_t)(const int32_t rpm[4]);`

## 硬件接线

| ESP8266 | 控制板（RC 接口 / USART2） | 说明 |
|---------|---------------------------|------|
| GPIO1（TX） | RC 信号（RC RX） | 单片机发 → 控制板收 |
| GPIO3（RX） | RC 信号（RC TX） | 控制板发 → 单片机收 |
| GND | GND | **必须共地** |

> 若 GPIO1/3 与 boot 日志/烧录冲突，可在 `setup()` 中 `Serial.swap()` 换到 GPIO15(TX)/GPIO13(RX)。接线接控制板 **RC 接口（USART2）**，不是 USB 口。

## 控制板预配置

用 USB 串口（波特率 **2000000-8N1**）连接控制板后执行：**`/uart2 115200 0 uart`**。
上电后本例程发送 `md_lite_subscribe(100)`，控制板周期推送 0xF0，`on_speed(rpm)` 打印到串口。

## 依赖与 mdc_lite

本目录内置 `mdc_lib.h/.cpp`、`mdc_lite.h/.cpp`、`mdc_lite_ctrl.h/.cpp`，与 `.ino` 同目录；
Arduino IDE 打开 `.ino` 自动编译。串口收发由本工程负责。

## 编译上传

1. 安装 ESP8266 开发板包（**文件 → 首选项 → 附加开发板管理器网址** 填
   `https://arduino.esp8266.com/stable/package_esp8266com_index.json`，然后安装 **esp8266 by ESP8266 Community**）。
2. 板型选你的板子（如 **NodeMCU 1.0** / **LOLIN(WEMOS) D1 mini**），端口选对应 COM 口。
3. 打开 `mdc_lite_callback.ino`，点「上传」；串口监视器设 **115200**、换行符「换行」。

## 运行说明

上电后自动订阅并开始每 50ms 发一帧控制；控制板每 100ms 推送 0xF0，回调打印如：

```
[rpm] 1234,-567,0,9000
```

> 注意：`on_speed` 中的 `rpm` 数组在本次回调内有效，如需跨回调保存请复制。
> 串口 RX 与 TX 为独立方向，TX 上的调试打印不会干扰 RX 上的二进制帧解析。
