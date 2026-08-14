# 51 单片机例程 — Keil C51（UART1 中断收发，9600 波特率连 RC 口）

基于 **AT89C52 / STC89C52（Keil C51，11.0592MHz 晶振）** 的 Motor Driver Controller 上位机例程：UART1（模式 1）以 **9600** 波特率连接控制板 **RC 接口（USART2）**，UART 中断接收、回车后整行转发，并演示文本指令与可选二进制控制帧。

> 本例程 **main.c 源码使用英文注释**（Keil 老版本对中文/UTF-8 兼容差，避免乱码），README 为中文。

## 功能

- **UART1 中断接收**：逐字节收进缓冲，收到回车（`\r` 或 `\n`）后判定整行完成，主循环将整行**原样转发**到 RC 口（透传测试用，见下方注意事项）。
- **演示函数发送文本指令**（上电自动执行一次）：
  - `/version`
  - `/mode 1 speed`
  - `/speedctrl 1 0.5 0.02 0.01`
  - `/save`
- **可选二进制帧示例**：`send_motor_ctrl_demo()`（0x31，int32 小端手动拼装 + CRC8）、`send_subscribe_demo()`（0x40）。**默认注释掉**，开启方法见下文。
- **CRC8** 严格按规范 §3.1 参考实现移植（多项式 0x07、初值 0、范围 = CMD+LEN+DATA）。

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

## 开发环境与编译（Keil 工程创建步骤）

1. 安装 **Keil C51**（µVision）。
2. **Project → New µVision Project**，命名保存。
3. 选择芯片：**Atmel → AT89C52**（STC 板选 **STC Micro → STC89C52RC**，需先安装 STC 器件包；也可直接选兼容的 AT89C52）。
4. 点 **New** 新建 `main.c`，把本目录 `main.c` 内容粘贴进去，加入工程（右键 Source Group → Add Existing Files…）。
5. **Target 选项**（右键 Target 1 → Options for Target）：
   - **Target 页**：Xtal 晶振填 **11.0592** MHz；Memory Model 选 Small。
   - **Output 页**：勾选 Create HEX File（生成 .hex 烧录文件）。
6. **Build（F7）** 编译，0 Error 后用 STC-ISP（STC 板）或其它编程器烧录 .hex。

### 波特率计算说明

- UART1 模式 1 波特率 = `晶振 / (32 × 12 × (256 − TH1))`（SMOD=0）。
- 11.0592MHz、9600 波特率：`256 − TH1 = 11059200 / (32×12×9600) = 3` → **TH1 = TL1 = 0xFD**。
- 换用其它晶振/波特率时按公式重算 TH1，并同步修改控制板 `/uart2 <波特率> 0 uart`。

## 运行与操作说明

1. 上电后，51 自动发送 4 条演示文本指令（/version、/mode 1 speed、/speedctrl…、/save），控制板应答（`OK (RAM only)`、版本信息等）会回显到 51 的 RXD。
2. 主循环把收到的完整行转发到 RC 口：适合**透传测试**——用 USB-TTL 转接（波特率 9600）临时接 51 的 UART，PC 串口工具输入 `/status` 回车，51 转发给控制板。
3. **开启 0x31 控制帧演示**：把 `main.c` 顶部 `#define SEND_CTRL_FRAME 0` 改为 `1`，重新编译烧录。上电后 51 会发送 `0x40 SUBSCRIBE(100ms)` 与一帧 `0x31`（ch1=开环 400、ch2=-400）。持续实时控制请参照其它例程按 30/50/100ms 周期循环调用 `send_motor_ctrl_demo()`（规范 §3.3）。
4. 修改目标值：直接改 `send_motor_ctrl_demo()` 里的 `targets[0..3]`。

## 代码结构

| 函数 | 说明 |
|------|------|
| `uart_init()` | SCON=0x50（模式 1 + 接收使能）、Timer1 模式 2、TH1/TL1=0xFD（9600）、开中断 |
| `uart_isr()` | 中断 4：收字节进 `rx_buf`，回车/换行置 `line_ok` |
| `send_char() / send_str() / send_line()` | 串口发送原语；`send_line` 自动补 `\n`（规范 §2.1） |
| `crc8()` | CRC8（0x07、初值 0、范围 CMD+LEN+DATA），按规范 §3.1 移植 |
| `put_i32_le()` | int32 小端手动拼装 |
| `send_frame()` | 组帧 `[0xAA][CMD][LEN][DATA][CRC8]` 并发送 |
| `send_motor_ctrl_demo()` | 0x31 四通道控制帧示例（默认关闭） |
| `send_subscribe_demo()` | 0x40 状态订阅示例（默认关闭） |
| `demo_send_text_cmds()` | 上电演示的 4 条文本指令 |
| `forward_line()` | 整行转发到 RC 口 |
| `delay_ms()` | 软件延时 |

## 用到的协议命令

| 类型 | 命令/帧 | 说明 |
|------|---------|------|
| 文本指令 | `/version` | 版本查询 |
| 文本指令 | `/mode 1 speed` | ch1 切速度闭环 |
| 文本指令 | `/speedctrl 1 0.5 0.02 0.01` | ch1 速度环 Kp/Ki/Kd |
| 文本指令 | `/save` | 配置写入 EEPROM |
| 文本指令 | `/uart2 9600 0 uart` | 控制板预配置（必需） |
| 二进制帧 | `0x31 MOTOR_CTRL` | `[m1~m4: 4×int32 LE]` 四通道控制（可选） |
| 二进制帧 | `0x40 SUBSCRIBE` | `[interval_ms:2B LE]` 状态上报（可选） |

## 常见问题

| 现象 | 原因 / 处理 |
|------|------------|
| 编译报错 `C249` / 中文乱码 | Keil 老版本默认 GB2312 编码，打开 UTF-8 文件会乱码——**本源码已用英文注释规避**；若自行加中文注释，请在 Keil 里设置 Edit → Configuration → Encoding 为 UTF-8（新版）或改用英文 |
| RC 口无响应 | 控制板未执行 `/uart2 9600 0 uart`；或波特率不匹配（51 是 9600，控制板必须也设 9600）；TX/RX 接反、未共地 |
| 收到应答后反复转发（回环） | 透传模式下控制板文本应答又被转发回 RC 口形成循环：注释掉 `main()` 里 `forward_line()` 调用，或改成只转发以 `/` 开头的行（代码注释里已给出写法） |
| 串口收不到 51 的数据 | 检查 USB-TTL 转接 RX/TX 是否交叉、波特率 9600、共地 |
| 波特率不对 | 晶振必须是 11.0592MHz 且 Keil Target 选项里已填 11.0592，否则按公式重算 TH1 |
| 电机不动 | 0x31 演示默认关闭（`SEND_CTRL_FRAME=0`）；开启后确认 `/mode` 已配置、优先级仲裁正常 |
| 内存不足 / DATA 溢出 | 默认 Small 模型只有 128B 内部 RAM；大帧缓冲需用 xdata（STC89C52RC 可用 `AUXR` 开启扩展 RAM）或选 Large 模型 |
