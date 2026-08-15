# 51 单片机例程 — Keil C51（UART1 中断收发，9600 波特率连 RC 口，mdc_lib 打包）

基于 **AT89C52 / STC89C52（Keil C51，11.0592MHz 晶振）** 的 Motor Driver Controller 单片机例程：UART1（模式 1）以 **9600** 波特率连接控制板 **RC 接口（USART2）**，UART 中断接收、回车后整行转发，并演示文本指令与可选二进制控制帧。**协议打包/解析全部由 mdc_lib 完成**，本工程只负责串口收发。

> 本例程 **main.c 源码使用英文注释**（Keil 老版本对中文/UTF-8 兼容差，避免乱码），README 为中文。

## 功能

- **UART1 中断接收**：逐字节收进缓冲，收到回车（`\r` 或 `\n`）后判定整行完成，主循环将整行**原样转发**到 RC 口（透传测试用，见下方注意事项）。
- **演示函数发送文本指令**（上电自动执行一次，全部由 `md_text_build` 构造、自动补 `\n`）：
  - `/version`
  - `/mode 1 speed`
  - `/speedctrl 1 0.5 0.02 0.01`
  - `/save`
- **可选二进制帧示例**：`md_bin_motor_ctrl()`（0x31 控制帧）与 `md_bin_subscribe()`（0x40 订阅，整帧含 CRC8）。**默认关闭**（`SEND_CTRL_FRAME=0`），开启方法见下文。
- 例程不含任何自写的 CRC8 / 组帧代码——全部由 mdc_lib 提供。

## 硬件接线（引脚表）

| 51 单片机 | 控制板（RC 接口 / USART2） | 说明 |
|-----------|---------------------------|------|
| P3.1（TXD） | RC 信号（RC RX） | 单片机发 → 控制板收 |
| P3.0（RXD） | RC 信号（RC TX） | 控制板发 → 单片机收 |
| GND | GND | **必须共地** |

文字图：

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

## 依赖与 mdc_lib

本例程依赖 **mdc_lib**（通用调用库，只做打包/解析，不依赖任何 Keil 外设头文件）。需要两个文件：

- `mdc_lib/51/keil/mdc_lib.h`
- `mdc_lib/51/keil/mdc_lib.c`

**加入 Keil 工程步骤**（在 `release/site/motor_driver_control/` 目录下执行，Windows）：

```
copy mdc_lib\51\keil\mdc_lib.h 例程\mcu\51_mcu\
copy mdc_lib\51\keil\mdc_lib.c 例程\mcu\51_mcu\
```

然后在 Keil µVision 中：

1. 把 `mdc_lib.c` 加入工程：右键 Source Group 1 → **Add Existing Files…** 选择 `mdc_lib.c`。
2. 添加头文件路径：**Options for Target → C51 → Include Paths** 添加 `mdc_lib.h` 所在目录。
3. （推荐）**Options for Target → C51 → Preprocessor Symbols → Define** 填入 `MD_ENABLE_CONFIG=0`：本例程不用 config 全字段函数，这样 `mdc_lib.c` 会编译掉 `md_pack_config` / `md_parse_config` / `md_bin_write_param` 及 231B 的 xdata 缓冲，省 RAM/代码空间。`main.c` 里也已预置该宏（仅作用于 main.c，工程级定义两者都生效）。

**内存说明（重要）**：8051 内部 RAM 很小（SMALL 模型仅 128B 直接寻址）。本例程的 `rx_buf[32]` + `tx_buf[40]` 放在默认 data 空间，普通 AT89C52 可编译通过；行缓冲因此限制为 31 字符。若需更长的命令行，可把两个缓冲改到 `xdata`（STC89C52RC 等有扩展 RAM 的型号）或减小缓冲。

## mdc_lib 调用指南

本例程用到的 mdc_lib API（签名与 `mdc_lib.h` 一致，C 系输出缓冲形态：返回「写入字节数」，`cap` 不足返回 0）：

| API | 用途 | 实际调用 |
|-----|------|---------|
| `md_text_build(cmd, args, out, cap)` | 任意文本指令（自动补 `\n`） | `md_text_build("/mode", "1 speed", tx_buf, sizeof(tx_buf))` → `/mode 1 speed\n` |
| `md_bin_motor_ctrl(t0..t3, out, cap)` | 0x31 四通道控制帧（整帧含 CRC8，可选演示） | `md_bin_motor_ctrl(400L, -400L, 0L, 0L, tx_buf, sizeof(tx_buf))` → 20B |
| `md_bin_subscribe(ms, out, cap)` | 0x40 订阅状态上报（可选演示） | `md_bin_subscribe(100u, tx_buf, sizeof(tx_buf))` → 6B |

发送骨架（与例程等价）：

```c
/* 文本指令：mdc_lib 构造 → 逐字节发送 */
unsigned int n = md_text_build("/status", 0, (char *)tx_buf, sizeof(tx_buf));
send_packed(tx_buf, n);                 /* send_packed 见 main.c */

/* 二进制控制帧：mdc_lib 打包整帧（同步字 + CRC8 都在库内） */
n = md_bin_motor_ctrl(400L, -400L, 0L, 0L, tx_buf, sizeof(tx_buf));
send_packed(tx_buf, n);
```

如需解析控制板推送的二进制上报帧（如 0xF0 状态帧），在 `main.c` 顶部声明 `xdata md_parser_t g_parser;`，在 UART 中断里逐字节喂 `md_parser_feed()`，返回 1 时用 `md_parse_status()` 解析（`payload` 在下次 feed 前有效）。注意解析器缓冲默认 256B 需放 xdata，且 `MD_PARSER_BUF` 小于 235 时无法完整解析 READ_PARAM 的 231B 应答，详见 `mdc_lib/51/keil/README.md`。

## 开发环境与编译（Keil 工程创建步骤）

1. 安装 **Keil C51**（µVision）。
2. **Project → New µVision Project**，命名保存。
3. 选择芯片：**Atmel → AT89C52**（STC 板选 **STC Micro → STC89C52RC**，需先安装 STC 器件包；也可直接选兼容的 AT89C52）。
4. 点 **New** 新建 `main.c`，把本目录 `main.c` 内容粘贴进去，加入工程（右键 Source Group → Add Existing Files…）；同时把 `mdc_lib.c` 也加入工程。
5. **Target 选项**（右键 Target 1 → Options for Target）：
   - **Target 页**：Xtal 晶振填 **11.0592** MHz；Memory Model 选 Small。
   - **Output 页**：勾选 Create HEX File（生成 .hex 烧录文件）。
   - **C51 页**：按上文在 Include Paths 添加 mdc_lib.h 目录；Preprocessor Symbols → Define 填 `MD_ENABLE_CONFIG=0`。
6. **Build（F7）** 编译，0 Error 后用 STC-ISP（STC 板）或其它编程器烧录 .hex。

### 波特率计算说明

- UART1 模式 1 波特率 = `晶振 / (32 × 12 × (256 − TH1))`（SMOD=0）。
- 11.0592MHz、9600 波特率：`256 − TH1 = 11059200 / (32×12×9600) = 3` → **TH1 = TL1 = 0xFD**。
- 换用其它晶振/波特率时按公式重算 TH1，并同步修改控制板 `/uart2 <波特率> 0 uart`。

## 运行与操作说明

1. 上电后，51 用 `md_text_build` 构造并发送 4 条演示文本指令（/version、/mode 1 speed、/speedctrl…、/save），控制板应答（`OK (RAM only)`、版本信息等）会回显到 51 的 RXD。
2. 主循环把收到的完整行转发到 RC 口：适合**透传测试**——用 USB-TTL 转接（波特率 9600）临时接 51 的 UART，PC 串口工具输入 `/status` 回车，51 转发给控制板。
3. **开启 0x31 控制帧演示**：把 `main.c` 顶部 `#define SEND_CTRL_FRAME 0` 改为 `1`，重新编译烧录。上电后 51 会发送 `md_bin_subscribe(100)` 与一帧 `md_bin_motor_ctrl`（ch1=开环 400、ch2=-400）。持续实时控制请参照其它例程按 30/50/100ms 周期循环调用 `send_motor_ctrl_demo()`（规范 §3.3）。
4. 修改目标值：直接改 `send_motor_ctrl_demo()` 里 `md_bin_motor_ctrl(400L, -400L, 0L, 0L, ...)` 的参数。

## 代码结构

| 函数/对象 | 说明 |
|------|------|
| `uart_init()` | SCON=0x50（模式 1 + 接收使能）、Timer1 模式 2、TH1/TL1=0xFD（9600）、开中断 |
| `uart_isr()` | 中断 4：收字节进 `rx_buf`，回车/换行置 `line_ok` |
| `send_char() / send_packed()` | 串口发送原语；`send_packed` 把 mdc_lib 打包结果逐字节发出 |
| `send_text_cmd(cmd, args)` | `md_text_build` 构造文本指令（自动补 `\n`）并发送 |
| `demo_send_text_cmds()` | 上电演示的 4 条文本指令 |
| `send_motor_ctrl_demo()` / `send_subscribe_demo()` | `md_bin_motor_ctrl` / `md_bin_subscribe` 二进制帧演示（`SEND_CTRL_FRAME=1` 时启用） |
| `forward_line()` | 整行转发到 RC 口 |
| `delay_ms()` | 软件延时 |
| `tx_buf[40]` | mdc_lib 打包输出缓冲（文本行最大 30B、0x31 帧 20B） |

## 常见问题

| 现象 | 原因 / 处理 |
|------|------------|
| 编译报错 `C249` / 中文乱码 | Keil 老版本默认 GB2312 编码，打开 UTF-8 文件会乱码——**本源码已用英文注释规避**；若自行加中文注释，请在 Keil 里设置 Edit → Configuration → Encoding 为 UTF-8（新版）或改用英文 |
| 找不到 `mdc_lib.h` | Include Paths 没加；或没把 `mdc_lib.h` / `mdc_lib.c` 复制到工程目录 |
| 编译报 `undefined symbol: md_text_build` 等 | `mdc_lib.c` 没有加入工程（右键 Source Group → Add Existing Files…） |
| RC 口无响应 | 控制板未执行 `/uart2 9600 0 uart`；或波特率不匹配（51 是 9600，控制板必须也设 9600）；TX/RX 接反、未共地 |
| 收到应答后反复转发（回环） | 透传模式下控制板文本应答又被转发回 RC 口形成循环：注释掉 `main()` 里 `forward_line()` 调用，或改成只转发以 `/` 开头的行（代码注释里已给出写法） |
| 串口收不到 51 的数据 | 检查 USB-TTL 转接 RX/TX 是否交叉、波特率 9600、共地 |
| 波特率不对 | 晶振必须是 11.0592MHz 且 Keil Target 选项里已填 11.0592，否则按公式重算 TH1 |
| 电机不动 | 0x31 演示默认关闭（`SEND_CTRL_FRAME=0`）；开启后确认 `/mode` 已配置、优先级仲裁正常 |
| 内存不足 / DATA 溢出 | 默认 Small 模型只有 128B 内部 RAM；本工程用 `rx_buf[32]` + `tx_buf[40]`；若自行加大缓冲，请放 xdata（STC89C52RC 可用 `AUXR` 开启扩展 RAM）或选 Large 模型；同时建议按上文在工程 Define 里加 `MD_ENABLE_CONFIG=0` 省 231B xdata |
