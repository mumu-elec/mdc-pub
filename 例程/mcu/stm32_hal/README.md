# STM32 HAL 例程 — CubeMX 工程集成（USART2 连 RC 口 + mdc_lib 打包/解析）

基于 **STM32F1/F4 + STM32Cube HAL** 的 Motor Driver Controller 单片机例程。例程按 **STM32F103C8T6（蓝板）** 编写，F4 芯片用法完全相同（仅 CubeMX 选型不同）。**协议打包/解析全部由 mdc_lib 完成**（零 HAL 依赖，纯 C），串口收发由本工程实现。

## 功能

- **主循环实时控制**：每 50ms 用 `md_bin_motor_ctrl` 打包并发送一帧 `0x31 MOTOR_CTRL`（ch1=200、ch2=-200、ch3/ch4=0，满足规范 §3.3「实时控制以 30/50/100ms 间隔连续发送」）。
- **状态上报解析**：上电用 `md_bin_subscribe(100)` 订阅，USART2 接收中断逐字节喂 `md_parser_feed()`（自动找 0xAA 同步 + CRC 校验）；收到 `0xF0` 用 `md_parse_status()` 解析 enc/tgt/rpm，收到 ACK 用 `md_parse_ack()` 解析，结果在主循环打印（不占用中断上下文）。
- **文本指令示例**：注释中给出 `md_text_mode` / `md_text_build` 的调用写法（把 ch1/ch2 切到速度闭环、调 PID 等）。
- 例程不含任何自写的 CRC8 / 组帧 / 帧解析代码——全部由 mdc_lib 提供。

## 硬件接线（引脚表）

| STM32（USART2） | 控制板（RC 接口 / USART2） | 说明 |
|-----------------|---------------------------|------|
| PA2（USART2_TX） | RC 信号（RC RX） | 单片机发 → 控制板收 |
| PA3（USART2_RX） | RC 信号（RC TX） | 控制板发 → 单片机收 |
| GND | GND | **必须共地** |

文字图：

```
STM32F103C8T6                   Motor Driver Controller
┌──────────────┐                ┌──────────────────┐
│ PA2 USART2_TX├───────────────►│ RC(RX)  USART2    │
│ PA3 USART2_RX│◄───────────────┤ RC(TX)            │
│ GND          ├───────────────►│ GND               │
└──────────────┘                └──────────────────┘
```

> 也可以换用其它 USART（如 USART1/PA9、PA10），只需在 CubeMX 里改外设，并把 `main_example.c` 中的 `huart2` 换成对应句柄。

## 控制板预配置

控制板 USART2 默认为遥控模式，需先用 USB 串口（波特率 **2000000-8N1**）连接控制板并执行：

```
/uart2 115200 0 uart
```

- 波特率 115200、极性正常、模式 uart（设置后立即生效并应用）。
- 默认控制优先级 **USART2 优先**（`/priority 0`），因此本例程从 USART2 发出的 0x31 控制帧天然生效；PC 端串口工具同时发控制帧时需注意仲裁（规范 §1、§3.4）。
- 需要时可用 `/timeout 0` 关闭超时归零保护。

## 依赖与 mdc_lib

本例程依赖 **mdc_lib**（通用调用库，只做打包/解析，不 include 任何 HAL 头文件）：

- `mdc_lib.h` / `mdc_lib.c` **已随例程内置（本目录，与 `Core/` 同级）**，无需手动复制。
- 在 CubeMX 生成的工程（CubeIDE / Keil）中把这两个文件**加入编译**即可：右键源文件组 → **Add Existing Files to Group…** 选择本目录的 `mdc_lib.c` 与 `mdc_lib.h`（CubeIDE 会自动扫描源文件目录；若文件不在其扫描范围内需手动加入）。
- 同时确认 `mdc_lib.h` 所在目录在 Include Paths 中（CubeIDE 默认包含；Keil 在 Options for Target → C/C++ → Include Paths 添加）。
- 如需更新库版本，用 `../../../mdc_lib/stm32/hal/` 下的同名文件覆盖本目录文件即可。
- 代码中 `#include "mdc_lib.h"` 即可使用全部 `md_*` API，无需链接任何额外库。
- 把 `Core/Src/main_example.c` 中的关键内容**合并进 CubeMX 生成的 `main.c`**（见「开发环境与编译」）。

## mdc_lib 调用指南

本例程用到的 mdc_lib API（签名与 `mdc_lib.h` 一致，C 系输出缓冲形态：返回「写入字节数」，`cap` 不足返回 0；解析函数返回 1/0）：

| API | 用途 | 实际调用 |
|-----|------|---------|
| `md_bin_motor_ctrl(t0..t3, out, cap)` | 0x31 四通道控制帧（整帧含 CRC8） | `md_bin_motor_ctrl(200, -200, 0, 0, buf, sizeof(buf))` → 20B |
| `md_bin_subscribe(ms, out, cap)` | 0x40 订阅状态上报 | `md_bin_subscribe(100, buf, sizeof(buf))` → 6B |
| `md_text_mode(ch, mode, out, cap)` | 控制模式切换 | `md_text_mode(1, "speed", line, sizeof(line))` → `/mode 1 speed\n` |
| `md_text_build(cmd, args, out, cap)` | 任意文本指令（自动补 `\n`） | `md_text_build("/speedctrl", "1 0.5 0.02 0.01", line, sizeof(line))` |
| `md_parser_init(&p)` | 初始化流式解析器 | `md_parser_init(&g_parser)` |
| `md_parser_feed(&p, byte, &cmd, &payload, &plen)` | 逐字节喂入，完整帧返回 1 | 接收中断里每字节调用 |
| `md_parse_status(payload, len, &st)` | 解析 0xF0（56B/72B 自动兼容） | 返回 1 后读 `st.enc[]/st.tgt[]/st.rpm[]` |
| `md_parse_ack(payload, len, &ack)` | 解析 ACK | 返回 1 后读 `ack.cmd / ack.err` |

发送/接收骨架（与例程等价）：

```c
static md_parser_t g_parser;

/* 发送：库打包 → HAL 串口写出 */
uint8_t buf[32];
uint16_t n = md_bin_motor_ctrl(200, -200, 0, 0, buf, sizeof(buf));
HAL_UART_Transmit(&huart2, buf, n, 100);

/* 接收中断：每字节喂流式解析器 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    uint8_t cmd; const uint8_t* payload; uint16_t plen;
    if (huart->Instance == USART2) {
        if (md_parser_feed(&g_parser, s_rx_byte, &cmd, &payload, &plen)) {
            if (cmd == MD_CMD_STATUS_REPORT) {          /* 0xF0 */
                md_status_t st;
                if (md_parse_status(payload, plen, &st) == 1) {
                    /* 消费 st.enc[]/st.rpm[]...（payload 在下次 feed 前有效） */
                }
            }
        }
        HAL_UART_Receive_IT(&huart2, &s_rx_byte, 1);
    }
}
```

> **注意：** `md_parser_feed` 返回 1 时，`payload` 指向解析器内部缓冲，必须在**下一次 feed 之前**消费（例程在中断内立即调用 `md_parse_status` 拷进结构体）。若要跨中断保存原始数据，请 `memcpy` 出来。

## 开发环境与编译

### 1. CubeMX 工程配置步骤

1. 新建工程，选择芯片（例程为 **STM32F103C8T6**；F4 选对应型号如 STM32F401CCU6，引脚/外设用法相同）。
2. **RCC**：HSE → Crystal/Ceramic Resonator。
3. **USART2**（或你选的 USARTx）：
   - Mode → Asynchronous（异步）
   - 参数：**Baud Rate 115200，Data 8，Parity None，Stop 1**（115200-8N1）
   - NVIC Settings → 勾选 **USART2 global interrupt** 使能中断
4. **时钟**：HSE 8MHz → PLL → SYSCLK 72MHz（CubeMX 自动生成 `SystemClock_Config`）。
5. 生成代码（Project Manager → Toolchain 选 MDK-ARM 或 STM32CubeIDE）。

### 2. 把 mdc_lib 加入工程

按上文「依赖与 mdc_lib」，把已内置（本目录）的 `mdc_lib.h` / `mdc_lib.c` 加入工程编译（Add Existing Files to Group），并确保 `mdc_lib.h` 所在目录在 Include Paths 中。

### 3. 合并 main_example.c 到 main.c

- 在 `main.c` 里 `#include "mdc_lib.h"`。
- 在 `main()` 里：`md_parser_init(&g_parser)` → `md_bin_subscribe` → `HAL_UART_Receive_IT` → 主循环 50ms 发 0x31。
- 添加 `HAL_UART_RxCpltCallback()`（CubeMX 生成的 `stm32f1xx_it.c` 已调用 `HAL_UART_IRQHandler`，HAL 会自动回调本函数）。
- 添加 0xF0/ACK 打印逻辑（`g_status` / `g_ack` / `g_frame_kind` 三个全局变量）。
- 若用 Keil MDK：建议勾选 **Target → Code Generation → Use MicroLIB**（配合示例中的 `fputc` 重定向即可用 `printf`）。

### 4. 编译烧录

- MDK：F7 编译 → F8 下载（需 ST-Link / DAP-Link + 对应 Flash 算法）。
- STM32CubeIDE：Build → Run。
- 烧录后按「运行与操作说明」验证。

## 运行与操作说明

1. 上电后，STM32 发送 `md_bin_subscribe(100)` 订阅状态上报，并每 50ms 发送一帧 0x31（ch1=200、ch2=-200，单位取决于各通道模式）。
2. 若已把 ch1/ch2 配置为速度闭环（取消注释 `md_text_mode(1,"speed")` / `md_text_mode(2,"speed")`，或在控制板执行 `/mode 1 speed`、`/mode 2 speed`），两个电机应分别以 200 / -200 RPM 转动。
3. 控制板每 100ms 推送一帧 0xF0，STM32 解析后通过 `printf` 打印 `0xF0 enc=... tgt=... rpm=...`（打印与 0x31 控制帧共用 USART2 线，接 USB-TTL 转 115200 查看；若控制板也在同一条线上收打印内容，其文本解析器会忽略非 `0xAA` 帧头字节，不影响控制）。
4. 修改目标值：直接改 `main_example.c` 主循环里 `md_bin_motor_ctrl(200, -200, 0, 0, ...)` 的参数后重新编译烧录；或把目标值改成全局变量，通过按键/其它接口在线修改。

## 代码结构

| 文件 | 内容 |
|------|------|
| `mdc_lib.c`（内置，本目录） | mdc_lib 实现：CRC8/组帧/帧解析、流式解析器、文本指令层、15 个二进制打包函数、解析层（加入工程即可用） |
| `Core/Src/main_example.c` | 集成示例：`uart2_send()`（打包结果 → HAL_UART_Transmit）、`main()` 主循环（订阅 + 50ms 发 0x31 + 0xF0/ACK 打印）、`HAL_UART_RxCpltCallback()`（逐字节喂 `md_parser_feed`） |
| `mdc_lib.h`（内置，本目录） | mdc_lib 头文件：常量、`md_status_t` / `md_ack_t` / `md_parser_t` 结构体、全部函数声明 |

## 常见问题

| 现象 | 原因 / 处理 |
|------|------------|
| 编译报错找不到 `mdc_lib.h` | 确认已复制头文件到 `Core/Inc`，且 Include Paths 包含 `Core/Inc` |
| 编译报错 `undefined symbol: md_xxx` | `mdc_lib.c` 未加入源文件组（Keil 需手动 Add Existing Files） |
| 编译报错 `main.c` 与示例函数重名 | `main_example.c` 是集成参考：只合并内容，不要把整个文件加进工程与 main.c 冲突 |
| HAL 版本差异 | 不同 Cube 版本的 HAL 函数名基本一致；老版本若 `HAL_UART_Receive_IT` 参数有差异，请以你的 HAL 版本为准；`fputc` 重定向写法（MicroLIB）各版本兼容 |
| F4 与 F1 差异 | 例程代码与芯片无关，仅需在 CubeMX 选型与时钟配置上按 F4 调整；引脚 PA2/PA3 在多数 F4 上仍是 USART2，但请以你芯片的数据手册为准 |
| printf 不输出 | 勾选 MicroLIB + 重写 `fputc`；或改用 `HAL_UART_Transmit` 直接发字符串 |
| 电机不动 | 确认控制板已 `/uart2 115200 0 uart` 且共地；确认 `/mode` 已配置；确认优先级仲裁（规范 §3.4） |
| 没有 0xF0 打印 | 确认 `md_bin_subscribe` 已执行且间隔 ≥20ms；0xF0 payload 常规 56B / 扩展 72B，`md_parse_status` 按 len 自动兼容 |
| 控制帧时灵时不灵 | 控制板处于协议自动识别期间会拒绝控制帧，等待识别完成 |
