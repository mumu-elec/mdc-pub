# 04_motor_control — 实时电机控制

## 功能

- 以固定周期（`--interval`，默认 50ms）**连续发送 0x31 MOTOR_CTRL 帧**（四通道 int32 小端目标值）——这是固件实时控制的核心帧。
- 斜坡平滑：每次发送向目标值逼近 `--ramp` 步长（默认 50），避免阶跃冲击；`--ramp 0` 直接跳变。
- 运行中键盘控制（非阻塞）：

| 按键 | 作用 |
|------|------|
| `+` | 目标值增大 `--step`（默认 100） |
| `-` | 目标值减小 `--step` |
| `0` | 目标清零 |
| `q` | 退出（退出前发送全零归零帧） |

- 启动时自动发送文本指令 `/priority 1` 并等待回显：上位机（USB）做实时控制必须获得控制仲裁优先权（协议规范 §1），否则 0x31 控制帧可能因 USART2 优先被拒绝。

## 控制目标含义（随通道控制模式）

| 模式 | 单位 | 范围 | 说明 |
|------|------|------|------|
| `open` | PWM | ±1000 | 开环直驱（旁路 PID） |
| `speed` | RPM | 无硬限幅 | 速度闭环 |
| `pos` | 0.1° | ±3600（=±360.0°） | 位置闭环（0.1° 精度） |

## 硬件与环境要求

| 项目 | 要求 |
|------|------|
| 硬件 | Motor Driver Controller（固件 v1.2.0+），USB Type-C 线，电机 + 编码器 |
| 驱动 | CH340N 虚拟串口驱动 |
| Python | 3.8+ |
| 依赖 | pyserial（唯一依赖） |

## 依赖安装

```bash
pip install -r requirements.txt
```

## 运行方法

```bash
# 开环：通道1 PWM 300（斜坡 50/周期，50ms 一帧 → 约 1s 爬满）
python motor_control.py --mode open --ch 1 --target 300

# 速度闭环：目标 500 RPM
python motor_control.py --mode speed --ch 1 --target 500 --ramp 100

# 位置闭环：目标 180.0°（0.1° 单位 = 1800）
python motor_control.py --mode pos --ch 1 --target 1800 --ramp 200

# 指定串口 / 更快周期 / 更大斜坡
python motor_control.py --port COM5 --mode speed --interval 30 --ramp 200
```

参数说明：

| 参数 | 说明 |
|------|------|
| `--port` | 串口号；缺省自动选择第一个 CH340 |
| `--mode` | `open`（默认）/ `speed` / `pos` |
| `--ch` | 通道 1~4（默认 1），其余通道恒发 0 |
| `--target` | 初始目标值（默认 0） |
| `--interval` | 发送间隔 ms（默认 50；规范建议 30/50/100ms） |
| `--ramp` | 斜坡步进/周期（默认 50；0 = 直接跳变） |
| `--step` | `+/-` 键目标增减量（默认 100） |

## ⚠️ 速度/位置闭环前必须配置 CPR 与 PID

开环可直接运行；速度/位置闭环前请先用文本指令配置（本目录 `02_text_commands` 可交互发送，也可在控制脚本运行前用任意串口工具发送）：

```bash
/cpr 1 500                 # 通道1 编码器线数（每转物理刻度数）
/speedctrl 1 0.5 0.02 0.01 # 速度环 Kp/Ki/Kd（示例值，按实际电机调整）
/posctrl 1 0.1 0.01 0.0    # 位置环 Kp/Ki/Kd（可选，位置环用）
/mode 1 speed              # 或 pos
/save                      # 持久化到 EEPROM（否则重启丢失）
```

> 各指令语法见协议规范 §2.3；`/speedctrl` 可选参数 `ilim/olim/period/ptype` 按需追加。

## 代码结构

| 函数/类 | 作用 |
|---------|------|
| `crc8()` / `build_frame()` | CRC8（poly 0x07，初值 0）与组帧（自包含实现） |
| `KeyReader` | 跨平台非阻塞按键（Windows msvcrt / POSIX termios） |
| `MotorController.send_ctrl()` | 发送 0x31 帧（4×int32 LE） |
| `MotorController.ramp_step()` | 斜坡逼近（每次移动 ≤ ramp） |
| `MotorController.run()` | 主循环：发帧 → 刷新状态 → 键盘 → 节拍 |
| `MotorController.stop()` | 退出前连发全零归零帧并关闭串口 |

## 用到的协议命令

- 文本指令：`/priority 1`（启动时自动发送，USB 主控；配置闭环还用到 `/cpr`、`/speedctrl`、`/posctrl`、`/mode`、`/save`）。
- 二进制命令：`0x31 MOTOR_CTRL`（核心控制帧，int32 LE ×4，受优先级仲裁）。

## 常见问题

| 现象 | 处理 |
|------|------|
| 发控制帧电机不动 | ① 检查 `/priority 1`（本脚本已自动发送，确认回显 OK）；② 检查 `/timeout` 未超时归零；③ 协议识别（/detect）期间控制帧被拒绝，等待识别完成 |
| 速度/位置模式不动或乱转 | 未配置 CPR/PID：执行上文的 `/cpr` + `/speedctrl`（+`/posctrl`），并 `/save` |
| 电机抖动/爬坡慢 | 减小 `--ramp` 或增大 `--interval`；PID 参数需按电机调整 |
| 退出后电机仍在转 | 本脚本退出前已发送全零帧；若仍异常，检查 `/timeout` 设置（设 500ms 可兜底归零） |
| 按键无响应（Linux） | 确认终端为 TTY（真实终端运行，非重定向）；Windows 无此问题 |

> 协议细节以 [`common/协议规范.md`](../../common/协议规范.md) 为准。
