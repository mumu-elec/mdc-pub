# STM32 HAL 例程 —— 完整 / 极简控制 / 控制+回调

基于 **STM32F1/F4 + STM32Cube HAL** 的 Motor Driver Controller 单片机例程。所有例程只用
**mdc_lib / mdc_lite** 打包与解析（纯 C，零 HAL 依赖），串口收发由本工程实现。

## 目录导览

| 类别 | 目录 | 用哪个库 | 说明 |
|------|------|---------|------|
| **完整** | [`完整/`](./完整/) | `mdc_lib` | 完整协议示例：文本指令 + 二进制命令 + 0xF0 解析 + ACK（既有 CubeMX 工程，含 `Core/Src/main_example.c` 集成示例） |
| **极简控制** | [`极简控制/`](./极简控制/) | `mdc_lite`（send-only） | 最小程序：只演示**发送控制帧**（0x31/0x40/0x41），不解析回包 |
| **控制+回调** | [`控制+回调/`](./控制+回调/) | `mdc_lite_ctrl` | 最小程序：**发送控制帧 + 流式收 0xF0 速度回调** |

> 三类按功能级别拆分：想一次看全协议（含文本指令 / 配置读写 / SBUS 等）用 `完整/`；
> 只想「上位机调参、下位机执行」用 `极简控制/`；还要回读实时转速用 `控制+回调/`。

## 极简调用库（mdc_lite）速览

| API | 用途 |
|-----|------|
| `md_lite_ctrl(m0..m3, out, cap)` | 0x31 四通道控制帧（int32 LE，直接委托 `md_bin_motor_ctrl`） |
| `md_lite_stop(out, cap)` | `ctrl(0,0,0,0)` 全零控制帧（急停/退出） |
| `md_lite_subscribe(ms, out, cap)` | 0x40 开启状态上报（interval_ms 建议 ≥20） |
| `md_lite_unsubscribe(out, cap)` | 0x41 关闭状态上报 |
| `md_lite_ctrl_init(c, cb)` / `md_lite_ctrl_feed(c, byte)` | 注册速度回调 + 逐字节喂 0xF0 接收器 |

> 详细规范见 [`../../../mdc_lib/LITE.md`](../../../mdc_lib/LITE.md)；
> 本平台（STM32 HAL）实现的 README 见 [`../../../mdc_lib/stm32/hal/mdc_lite_README.md`](../../../mdc_lib/stm32/hal/mdc_lite_README.md)。

## 硬件接线（三类共用）

| STM32（USART2，示例用） | 控制板（RC 接口 / USART2） | 说明 |
|------------------------|---------------------------|------|
| PA2（USART2_TX） | RC 信号（RC RX） | 单片机发 → 控制板收 |
| PA3（USART2_RX） | RC 信号（RC TX） | 控制板发 → 单片机收 |
| GND | GND | **必须共地** |

控制板 USART2 需先用 USB 串口（2000000-8N1）执行 `/uart2 115200 0 uart`，默认 USART2 优先（`/priority 0`），
因此从 USART2 发出的 0x31 控制帧天然生效。
