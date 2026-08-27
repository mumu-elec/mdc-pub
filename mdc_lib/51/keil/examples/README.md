# 51 单片机例程 — Keil C51（mdc_lib / mdc_lite，UART1 中断收发，9600 波特率连 RC 口）

基于 **AT89C52 / STC89C52（Keil C51，11.0592MHz 晶振）** 的 Motor Driver Controller 单片机例程：UART1（模式 1）以 **9600** 波特率连接控制板 **RC 接口（USART2）**，UART 中断收发。**协议打包/解析全部由 mdc_lib / mdc_lite 完成**，例程只负责串口收发。

> 例程 **main.c 源码使用英文注释**（Keil 老版本对中文/UTF-8 兼容差，避免乱码），README 为中文。

## 例程导览（三档）

按功能级别分为三类（LITE.md §4）：

| 类别 | 目录 | 内容 | 用的库 |
|------|------|------|--------|
| **完整** | [`完整/`](完整/) | 完整协议示例：文本指令 `/version`、`/mode`、`/speedctrl`、`/save` + 可选 0x31 二进制控制帧；UART 中断逐行转发 | `mdc_lib` |
| **极简控制** | [`极简控制/`](极简控制/) | 只演示「发送控制帧」：`md_lite_ctrl` 打包 0x31 并发送（含 subscribe/stop），**不解析回包** | `mdc_lite`（send-only） |
| **控制+回调** | [`控制+回调/`](控制+回调/) | 演示「发控制帧 + 收速度回调」：`md_lite_ctrl_init/feed` 流式接收 0xF0，四通道 rpm 回调给用户 | `mdc_lite_ctrl` |

> 只需发送控制帧 → [极简控制](极简控制/)；还需回读实时转速 → [控制+回调](控制+回调/)；想看完整指令集与透传 → [完整](完整/)。

每个例程目录内已内置所需的库文件（`mdc_lib.h/.c`，以及 `极简控制` 的 `mdc_lite.h/.c`、`控制+回调` 的 `mdc_lite.h/.c + mdc_lite_ctrl.h/.c`），加入 Keil 工程即可。库的权威版本在 [`../../../../../../51/keil/`](../../../../../../51/keil/)（含 mdc_lib / mdc_lite 的 README 与说明）。

## 硬件接线（引脚表，三类例程通用）

| 51 单片机 | 控制板（RC 接口 / USART2） | 说明 |
|-----------|---------------------------|------|
| P3.1（TXD） | RC 信号（RC RX） | 单片机发 → 控制板收 |
| P3.0（RXD） | RC 信号（RC TX） | 控制板发 → 单片机收 |
| GND | GND | **必须共地** |

```
AT89C52/STC89C52                  Motor Driver Controller
┌─────────────┐                   ┌──────────────────┐
│ P3.1 TXD    ├──────────────────►│ RC(RX)  USART2    │
│ P3.0 RXD    │◄──────────────────┤ RC(TX)            │
│ GND         ├──────────────────►│ GND               │
└─────────────┘                   └──────────────────┘
```

> 接线接控制板 **RC 接口（USART2）**，不是 USB 口。TX/RX 交叉，GND 共地。

## 控制板预配置

控制板 USART2 默认为遥控模式，需先用 USB 串口（波特率 **2000000-8N1**）连接控制板并执行（**本例程用 9600，与控制板 9600 匹配更稳**）：

```
/uart2 9600 0 uart
```

- 波特率 9600、极性正常、模式 uart（设置后立即生效并应用）。
- 默认控制优先级 **USART2 优先**（`/priority 0`），51 从 RC 口发的控制帧天然生效。
- 需要时可用 `/timeout 0` 关闭超时归零保护。

## 常见问题

| 现象 | 原因 / 处理 |
|------|------------|
| 编译报错 `C249` / 中文乱码 | Keil 老版本默认 GB2312 编码，打开 UTF-8 文件会乱码——**本源码已用英文注释规避** |
| 找不到 `mdc_lib.h` / `mdc_lite.h` | Include Paths 没加；或没把库文件复制到工程目录（每个例程目录已内置） |
| 编译报 `undefined symbol: md_text_build / md_lite_ctrl` 等 | 对应的 `.c`（`mdc_lib.c`、`mdc_lite.c`、`mdc_lite_ctrl.c`）没有加入工程 |
| RC 口无响应 | 控制板未执行 `/uart2 9600 0 uart`；或波特率不匹配；TX/RX 接反、未共地 |
| 内存不足 / DATA 溢出 | 默认 Small 模型只有 128B 内部 RAM；按各例程 README 把大数组（解析器缓冲、发送缓冲）放 xdata，并在工程 Define 加 `MD_ENABLE_CONFIG=0` |

## 开发环境与编译（Keil 工程创建步骤，三类通用）

1. 安装 **Keil C51**（µVision）。
2. **Project → New µVision Project**，选择芯片：**Atmel → AT89C52**（STC 板选 **STC Micro → STC89C52RC**）。
3. 把对应例程目录里的 `main.c` 与库文件（`mdc_lib.c` + `mdc_lite.c` / `mdc_lite_ctrl.c`）加入工程（右键 Source Group → Add Existing Files…）。
4. **Target 选项**：Target 页 Xtal 晶振填 **11.0592** MHz、Memory Model 选 Small；C51 页在 Include Paths 添加头文件目录，并在 Preprocessor Symbols → Define 填 `MD_ENABLE_CONFIG=0`。
5. **Build（F7）** 编译，0 Error 后用 STC-ISP（STC 板）或其它编程器烧录 .hex。

### 波特率计算说明

- UART1 模式 1 波特率 = `晶振 / (32 × 12 × (256 − TH1))`（SMOD=0）。
- 11.0592MHz、9600 波特率：`256 − TH1 = 11059200 / (32×12×9600) = 3` → **TH1 = TL1 = 0xFD**。
- 换用其它晶振/波特率时按公式重算 TH1，并同步修改控制板 `/uart2 <波特率> 0 uart`。

## 与 mdc_lib 的关系

各例程的协议层全部由 [`mdc_lib`](../../../../../../51/keil/) 与极简层 [`mdc_lite`](../../../../../../51/keil/) 提供：它们只做字节打包/解析，不依赖任何 Keil 外设头文件，串口收发由例程实现。极简层是独立实现（字节布局、CRC8、帧格式完全一致），只是把关注范围收窄到「发送控制 + 接收 0xF0 转速」这 4 条命令。
