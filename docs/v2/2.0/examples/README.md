# MDC v2 配套例程

《Motor Driver Controller 技术手册 v2》（`../technical-manual.md`）配套例程。
Python 例程仅需 `pyserial`：

```
pip install pyserial
```

## 硬件连接

**方式一：PC + USB（Python 例程）**

```
PC ←── USB Type-C（数据线）──→ 驱动板 USB 口
      虚拟串口 CH340，固定 2000000-8N1
      ⚠ USB 仅通信不供电 —— 驱动板必须另接 DC 电源（5.5~15V，内正外负）
```

**方式二：外部 MCU + RC 口（`mdc_mcu_example.ino`）**

```
外部 MCU TX ──→ 驱动板 RC 接口（SIGNAL + GND，无极性可反插，3.3V/5V TTL 均可）
                 出厂默认 UART 115200 8N1；曾改过协议时长按 FUN 键 1s 重新识别
                 ⚠ RC 只传信号不供电；MCU 与驱动板必须共地
```

## 例程清单

| 文件 | 内容 | 手册章节 |
|------|------|---------|
| `mdc.py` | 协议最小库：CRC8、帧构建/解析、文本指令、遥测解析 | §5 |
| `01_ping_version.py` | 连接、PING、`/version`、`/check` | §5.2 / §5.4 |
| `02_params.py` | 全量读配置、WRITE_FIELD 改单字段、受保护区演示、`/save` | §5.5 / §5.7 |
| `03_motor_control.py` | 单电机 开环/速度/位置 三模式控制 | §4.1 / §5.5 |
| `04_chassis_control.py` | 底盘配置 + 整车 (vx, vy, ω) 向量控制 | §4.8 / §5.5 |
| `05_telemetry.py` | SUBSCRIBE 订阅并解析 0xF0 实时遥测（56B/72B） | §5.6 |
| `mdc_mcu_example.ino` | 外部 MCU 控制：文本配置 + 二进制 0x30/0x31 帧 | §3.3 / §5.5 |

运行方式（把 `SERIAL_PORT` 改成你的 CH340 端口号）：

```
python 01_ping_version.py
```

## 安全提示（重要）

1. **架空**：首次联调把轮子/底盘架空，确认方向与限幅后再落地；
2. **超时保护**：固件出厂 `cmd_timeout=0`（关闭）。例程都会先设 `/timeout 1000`——程序异常停发指令后电机会自动归零；
3. **速度/位置闭环前先配编码器**：`/cpr` 按规格书填线数（固件 ×4 倍频）；
4. **底盘先配减速比**：`/posangle = 线数 × 减速比`，否则实车速度慢 g 倍（`/chassis` 的 GEAR 行有提示）；
5. **开环档先标定 k_ch**（`/sbusparam`）：`/chassis` 的 MODE 行带 `(k_ch未标定?)` 时非零目标会饱和到满 PWM。

## 常见问题

| 现象 | 排查 |
|------|------|
| 打不开串口 | 端口号/被占用；设备管理器确认 CH340（驱动见板厂说明） |
| PING 无响应 | 驱动板未上电（USB 不供电）；波特率不是 2000000 |
| 文本指令无回复 | 已 SUBSCRIBE 时数据流会淹没文本回复，先 UNSUBSCRIBE |
| 电机转但 RPM 异常 | `/cpr` 未配或配错；编码器 A/B 接反可用 `/einv` 补偿 |
| 底盘车速不对 | 查 `/chassis` 的 GEAR（减速比）与 MODE 行；核对轮径 mm |
