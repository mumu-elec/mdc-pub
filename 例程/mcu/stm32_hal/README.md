# STM32 HAL 例程 — CubeMX 工程集成（USART 连 RC 口 + 协议封装层）

基于 **STM32F1/F4 + STM32Cube HAL** 的 Motor Driver Controller 上位机例程。例程按 **STM32F103C8T6（蓝板）** 编写，F4 芯片用法完全相同（仅 CubeMX 选型不同）。提供**不依赖具体外设的协议封装层**（纯函数 + 依赖注入回调）与主循环集成示例。

## 功能

- **协议封装层**（`motor_driver.h/.c`，可直接移植到任意 STM32 工程）：
  - `md_crc8()`：CRC8（多项式 0x07、初值 0，范围 = CMD+LEN+DATA），按规范 §3.1 移植
  - `md_build_frame()`：组帧 `[0xAA][CMD][LEN][DATA][CRC8]`
  - `motor_driver_send_uart(buf, len)`：依赖注入回调，由用户实现（调用 `HAL_UART_Transmit`）
  - `md_send_text()`、`md_motor_ctrl()`（0x31，int32 小端手动拼装）、`md_subscribe()`（0x40）、`md_read_param()`（0x10）、`md_save_eeprom()`（0x20）
  - `md_rx_byte()`：滑动窗口帧解析状态机（SYNC→CMD→LEN→DATA→CRC），CRC 通过后自动分发
  - `md_on_status_report()` / `md_on_read_param()`：**弱实现解析回调骨架**，用户可重写（0xF0 上报、config_t 231B 解析）
- **主循环示例**（`main_example.c`）：50ms 周期发送 0x31 控制帧、订阅 0xF0 并打印、USART2 接收中断逐字节喂解析器。

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

> 也可以换用其它 USART（如 USART1/PA9、PA10），只需在 CubeMX 里改外设并在 `motor_driver_send_uart` 里换句柄。

## 控制板预配置

控制板 USART2 默认为遥控模式，需先用 USB 串口（波特率 **2000000-8N1**）连接控制板并执行：

```
/uart2 115200 0 uart
```

- 波特率 115200、极性正常、模式 uart（设置后立即生效并应用）。
- 默认控制优先级 **USART2 优先**（`/priority 0`），因此本例程从 USART2 发出的 0x31 控制帧天然生效；若 USB 上位机同时发控制帧，需注意仲裁（规范 §1、§3.4）。
- 需要时可用 `/timeout 0` 关闭超时归零保护。

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

### 2. 把协议封装加入工程

- 将 `Core/Inc/motor_driver.h` 拷贝到工程 `Core/Inc/`，`Core/Src/motor_driver.c` 拷贝到 `Core/Src/`（或任意目录后在工程设置里加入 Include Paths）。
- 将 `main_example.c` 中的关键内容合并进 CubeMX 生成的 `main.c`：
  - 包含 `motor_driver.h`
  - 定义 `motor_driver_send_uart()`（依赖注入回调）
  - 合并（可选重写）`md_on_status_report()` / `md_on_read_param()`
  - 主循环里加 50ms 发送 0x31 的逻辑
  - 加 `HAL_UART_RxCpltCallback()` 接收中断回调
- 若用 Keil MDK：建议勾选 **Target → Code Generation → Use MicroLIB**（配合示例中的 `fputc` 重定向即可用 `printf`）。

### 3. 编译烧录

- MDK：F7 编译 → F8 下载（需 ST-Link / DAP-Link + 对应 Flash 算法）。
- STM32CubeIDE：Build → Run。
- 烧录后按「运行与操作说明」验证。

## 运行与操作说明

1. 上电后，STM32 每 50ms 向控制板发送一帧 0x31（ch1=200、ch2=-200，单位取决于各通道模式），并订阅了 100ms 状态上报。
2. 若已配置速度闭环（`/mode 1 speed`、`/mode 2 speed`），两个电机应分别以 200 / -200 RPM 转动。
3. 控制板每 100ms 推送一帧 0xF0，STM32 解析后通过 `printf` 打印 enc/tgt/rpm 前四路（需接 USART2 到串口工具查看，或自行改打印通道）。
4. 修改目标值：直接改 `main_example.c` 里的 `targets[4]` 数组后重新编译烧录；或把目标值改成全局变量，通过其它交互方式（按键/上位机）在线修改。

## 代码结构

| 文件 | 内容 |
|------|------|
| `Core/Inc/motor_driver.h` | 协议常量、函数接口声明、回调声明 |
| `Core/Src/motor_driver.c` | CRC8、组帧、文本指令、0x31/0x40/0x10/0x20 发送、滑动窗口帧解析、弱实现回调骨架 |
| `Core/Src/main_example.c` | 依赖注入回调实现、0xF0/0x10 解析示例、main 主循环示例、USART 接收中断回调 |

## 用到的协议命令

| 类型 | 命令/帧 | 说明 |
|------|---------|------|
| 二进制帧 | `0x31 MOTOR_CTRL` | `[m1~m4: 4×int32 LE]` 四通道控制（50ms 连续发送） |
| 二进制帧 | `0x40 SUBSCRIBE` | `[interval_ms:2B LE]` 开启 0xF0 周期上报 |
| 二进制帧 | `0x10 READ_PARAM` | 读取 config_t（231B） |
| 二进制帧 | `0x20 SAVE_EEPROM` | RAM 配置写 EEPROM |
| 二进制帧 | `0xF0 STATUS_REPORT` | 状态上报解析（enc/tgt/rpm） |
| 文本指令 | `/uart2 115200 0 uart` | 控制板预配置（必需） |
| 文本指令 | `/mode <ch> speed` | 切速度闭环（可选） |

## 常见问题

| 现象 | 原因 / 处理 |
|------|------------|
| 编译报错 `undefined symbol: motor_driver_send_uart` | 忘了在 main.c 里实现该依赖注入回调 |
| 编译报错 `main.c` 与示例函数重名 | `main_example.c` 是集成参考：只合并内容，不要把整个文件加进工程与 main.c 冲突 |
| HAL 版本差异 | 不同 Cube 版本的 HAL 函数名基本一致；老版本若没有 `HAL_UART_Receive_IT` 参数差异，请以你的 HAL 版本为准；`fputc` 重定向写法（MicroLIB）各版本兼容 |
| F4 与 F1 差异 | 例程代码与芯片无关，仅需在 CubeMX 选型与时钟配置上按 F4 调整；引脚 PA2/PA3 在多数 F4 上仍是 USART2，但请以你芯片的 datasheet 为准 |
| printf 不输出 | 勾选 MicroLIB + 重写 `fputc`；或改用 `HAL_UART_Transmit` 直接发字符串 |
| 电机不动 | 确认控制板已 `/uart2 115200 0 uart` 且共地；确认 `/mode` 已配置；确认优先级仲裁（规范 §3.4） |
| 收到 0xF0 但解析乱 | 确认 `md_subscribe` 已执行且间隔 ≥20ms；0xF0 payload 常规 56B / 扩展 72B，长度分支判断需正确 |
| 控制帧时灵时不灵 | 控制板处于协议自动识别期间会拒绝控制帧，等待识别完成 |
