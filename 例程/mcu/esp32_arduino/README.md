# ESP32 Arduino 例程 — UART2 连 RC 口（双 FreeRTOS 任务：实时控制 + 状态上报）

基于 **ESP32 Arduino** 的 Motor Driver Controller 上位机例程：用 HardwareSerial（UART2，TX=17、RX=16）连接控制板 **RC 接口（USART2）**，USB Serial 交互。两个 FreeRTOS 任务并行处理**控制帧发送**与**状态帧解析**。

## 功能

- **task_send**：每 **50ms** 连续发送一帧 `0x31 MOTOR_CTRL` 控制帧（满足规范 §3.3「实时控制以 30/50/100ms 间隔连续发送」）。目标值通过 USB Serial 命令实时修改。
- **task_recv**：读取 RC 口回显/上报，滑动窗口解析二进制帧；收到 `0xF0 STATUS_REPORT`（需先订阅）解析 enc/tgt/rpm 打印到 USB Serial。
- **USB Serial 命令**：

| 命令 | 动作 |
|------|------|
| `pwm 400` | 开环目标 PWM=400（ch1，需先 `/mode 1 open`） |
| `speed 200` | 速度目标 200RPM（ch1，需先 `/mode 1 speed`） |
| `stop` | 四通道目标清零 |
| `mode 1 speed` | 等价文本指令 `/mode 1 speed`（转发到 RC 口） |
| `/version` 等 | 任意以 `/` 开头的文本指令原样转发 |

- 协议封装函数：`crc8()`、`send_frame()`、`send_motor_ctrl()`（0x31，int32 小端手动拼装）、`subscribe()`（0x40）。

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
- 默认控制优先级 **USART2 优先**（`/priority 0`），ESP32 从 RC 口发出的 0x31 控制帧天然生效；USB 上位机同时发控制帧时需注意仲裁（规范 §1、§3.4）。
- 需要时可用 `/timeout 0` 关闭超时归零保护。

## 开发环境与编译

1. 安装 [Arduino IDE](https://www.arduino.cc/en/software)。
2. 安装 ESP32 开发板包：**文件 → 首选项 → 附加开发板管理器网址** 填入 `https://dl.espressif.com/dl/package_esp32_index.json`，然后 **工具 → 开发板 → 开发板管理器** 搜索 `esp32` 安装 **esp32 by Espressif Systems**。
3. 板型选择：**工具 → 开发板 → ESP32 Arduino → ESP32 Dev Module**。
4. 端口选择：**工具 → 端口 → 选择 ESP32 的 COM 口**（需先装 CP210x/CH340 驱动，视开发板而定）。
5. 打开 `motor_driver_esp32.ino`，点击「上传」。
6. 打开**串口监视器**，波特率 **115200**，换行符「换行」（Newline）。

## 运行与操作说明

1. 上电后 USB Serial 打印提示信息，ESP32 自动发送 `SUBSCRIBE(100ms)` 开启状态上报，并开始每 50ms 发送 0x31（初始目标全 0，电机不动）。
2. 输入 `mode 1 speed` → ch1 切速度闭环；再输入 `speed 200` → ch1 以 200RPM 转动。
3. 输入 `pwm 400`（配合 `/mode 1 open`）→ 开环驱动。
4. 控制板每 100ms 推送 0xF0，USB Serial 打印 `[0xF0] enc=... tgt=... rpm=...`。
5. 输入 `stop` 清零目标；任意 `/` 开头文本指令原样转发（如 `/status`）。

## 代码结构

| 函数/任务 | 说明 |
|-----------|------|
| `crc8()` | CRC8（多项式 0x07、初值 0，范围 = CMD+LEN+DATA），按规范 §3.1 移植 |
| `send_frame()` | 组帧 `[0xAA][CMD][LEN][DATA][CRC8]` 并发送 |
| `put_i32_le()` | int32 小端手动拼装（避免 union 对齐问题） |
| `send_motor_ctrl()` | 发送 0x31 四通道控制帧（16B DATA） |
| `subscribe()` | 发送 0x40，开启 0xF0 周期上报 |
| `rx_byte()` | 滑动窗口帧解析状态机（SYNC→CMD→LEN→DATA→CRC） |
| `handle_status_report()` | 0xF0 payload 解析（enc/tgt/rpm，56B 常规模式） |
| `task_recv` | FreeRTOS 任务：读 Serial2 → 帧解析 → 打印 |
| `task_send` | FreeRTOS 任务：每 50ms 发 0x31 |
| `handle_usb_line()` | USB Serial 命令分发 |

## 用到的协议命令

| 类型 | 命令/帧 | 说明 |
|------|---------|------|
| 二进制帧 | `0x31 MOTOR_CTRL` | `[m1~m4: 4×int32 LE]` 四通道控制（50ms 连续发送） |
| 二进制帧 | `0x40 SUBSCRIBE` | `[interval_ms:2B LE]` 开启 0xF0 周期上报 |
| 二进制帧 | `0xF0 STATUS_REPORT` | 状态上报解析（enc/tgt/rpm，56B/72B） |
| 文本指令 | `/uart2 115200 0 uart` | 控制板预配置（必需） |
| 文本指令 | `/mode <ch> open\|speed\|pos` | 控制模式切换 |
| 文本指令 | `/save` 等 | 任意文本指令转发 |

## 常见问题

| 现象 | 原因 / 处理 |
|------|------------|
| 上传失败 | 按住开发板 BOOT 键再点上传；检查端口/驱动 |
| RC 口无响应 | 控制板未执行 `/uart2 115200 0 uart`；TX/RX 接反、未共地 |
| 没有 0xF0 打印 | 确认 `subscribe(100)` 已执行、间隔 ≥20ms；确认收的是二进制帧而非文本 |
| 电机不动 | 目标全 0 时不会动；确认 `/mode` 已配置、优先级仲裁（规范 §3.4） |
| 乱码 | 串口监视器波特率 115200；换行符设「换行」 |
| 任务栈溢出警告 | 增大 `xTaskCreate` 的栈大小参数（4096 → 8192） |
