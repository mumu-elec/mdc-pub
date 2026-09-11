# 完整例程 — mdc_lib（文本指令 + 二进制控制帧 + 透传）

> **目录：** `mdc_lib/51/keil/examples/完整/`
> **定位：** 该平台**完整**的协议示例：文本指令、二进制控制帧、UART 中断逐行转发（透传测试）。
> **用的库：** [`mdc_lib`](../../../../../../../../51/keil/)（完整通用层）。

## 功能

- **UART1 中断接收**：逐字节收进缓冲，收到回车（`\r` 或 `\n`）后判定整行完成，主循环将整行**原样转发**到 RC 口（透传测试用）。
- **上电自动发送演示文本指令**（全部由 `md_text_build` 构造、自动补 `\n`）：
  - `/version`
  - `/mode 1 speed`
  - `/speedctrl 1 0.5 0.02 0.01`
  - `/save`
- **可选二进制帧**：`md_bin_motor_ctrl()`（0x31 控制帧）与 `md_bin_subscribe()`（0x40 订阅，整帧含 CRC8）。**默认关闭**（`SEND_CTRL_FRAME=0`），开启方法见下。
- 例程不含任何自写的 CRC8 / 组帧代码，全部由 mdc_lib 提供。

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

## 用到的 mdc_lib API（C 系输出缓冲形态：返回「写入字节数」，`cap` 不足返回 0）

| API | 用途 | 实际调用 |
|-----|------|---------|
| `md_text_build(cmd, args, out, cap)` | 任意文本指令（自动补 `\n`） | `md_text_build("/mode", "1 speed", tx_buf, sizeof(tx_buf))` → `/mode 1 speed\n` |
| `md_bin_motor_ctrl(t0..t3, out, cap)` | 0x31 四通道控制帧（整帧含 CRC8） | `md_bin_motor_ctrl(400L, -400L, 0L, 0L, tx_buf, sizeof(tx_buf))` → 20B |
| `md_bin_subscribe(ms, out, cap)` | 0x40 订阅状态上报 | `md_bin_subscribe(100u, tx_buf, sizeof(tx_buf))` → 6B |

发送骨架：

```c
/* 文本指令：mdc_lib 构造 → 逐字节发送 */
unsigned int n = md_text_build("/status", 0, (char *)tx_buf, sizeof(tx_buf));
send_packed(tx_buf, n);

/* 二进制控制帧：mdc_lib 打包整帧（同步字 + CRC8 都在库内） */
n = md_bin_motor_ctrl(400L, -400L, 0L, 0L, tx_buf, sizeof(tx_buf));
send_packed(tx_buf, n);
```

如需解析控制板推送的二进制上报帧（如 0xF0 状态帧），在 `main.c` 顶部声明 `xdata md_parser_t g_parser;`，在 UART 中断里逐字节喂 `md_parser_feed()`，返回 1 时用 `md_parse_status()` 解析（`payload` 在下次 feed 前有效）。解析器缓冲默认 256B 需放 xdata，且 `MD_PARSER_BUF` 小于 252 时无法完整解析 READ_PARAM 的 248B 应答。**若只想要「发控制 + 收转速」的极简流程，直接用 [`../极简控制/`](../极简控制/) 或 [`../控制+回调/`](../控制+回调/)。**

## 编译与运行

1. 按顶层 `README.md` 的 Keil 工程创建步骤，把本目录 `main.c` 与 `mdc_lib.c` 加入工程，Include Paths 指向本目录。
2. （推荐）工程级 Define 加 `MD_ENABLE_CONFIG=0`：本例程不用 config 全字段函数，这样 `mdc_lib.c` 会编译掉 `md_pack_config` / `md_parse_config` / `md_bin_write_param` 及 248B 的 xdata 缓冲，省 RAM/代码空间。
3. 上电后 51 发送 4 条演示文本指令，控制板应答会回显到 51 的 RXD。
4. **开启 0x31 控制帧演示**：把 `main.c` 顶部 `#define SEND_CTRL_FRAME 0` 改为 `1`，重新编译烧录。
5. 主循环把收到的完整行转发到 RC 口（透传）：用 USB-TTL 转接临时接 51 的 UART，PC 串口工具输入 `/status` 回车，51 转发给控制板。

## 内存说明

8051 内部 RAM 很小（SMALL 模型仅 128B 直接寻址）。本例程的 `rx_buf[32]` + `tx_buf[40]` 放在默认 data 空间，普通 AT89C52 可编译通过；行缓冲因此限制为 31 字符。若需更长的命令行，可把两个缓冲改到 `xdata`（STC89C52RC 等有扩展 RAM 的型号）或减小缓冲。
