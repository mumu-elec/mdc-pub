# 05_status_monitor — 状态监控

## 功能

- 发送 `0x40 SUBSCRIBE` 开启 STATUS_REPORT（0xF0）周期上报（`--interval`，默认 50ms；固件最低 20ms）。
- 后台读线程持续解析帧（滑动窗口找 `0xAA` + CRC8 校验），自动兼容两种 payload：
  - 常规 **56B**：`enc[4]i32 + tgt[4]f32 + rpm[4]i32 + sbus_frame_cnt:u32 + sbus_ok_cnt:u32`；
  - 扩展 **72B**：上述基础上追加 `rpm_raw[4]i32`（需先 `0x44 DEBUG_SPEED=1` 开启）；
  - 全部字段小端序。
- 控制台实时表格：用 `\r` / ANSI 光标回退覆盖刷新，显示 4 通道 `enc / tgt / rpm`（扩展模式下附加 rpm_raw）。
- 可选 `--csv`：每收到一帧追加一行记录（含时间戳与全部字段），便于离线分析。
- Ctrl+C 干净退出：停止读线程 → 发送 `0x41 UNSUBSCRIBE` → 关闭串口。

## 硬件与环境要求

| 项目 | 要求 |
|------|------|
| 硬件 | Motor Driver Controller（固件 v1.2.0+），USB Type-C 线 |
| 驱动 | CH340N 虚拟串口驱动 |
| Python | 3.8+ |
| 依赖 | pyserial（唯一依赖） |

## 依赖安装

```bash
pip install -r requirements.txt
```

## 运行方法

```bash
# 默认 50ms 周期
python status_monitor.py

# 指定串口 + 20ms 最快周期
python status_monitor.py --port COM5 --interval 20

# 同时记录 CSV
python status_monitor.py --csv log.csv
```

参数说明：

| 参数 | 说明 |
|------|------|
| `--port` | 串口号；缺省自动选择第一个 CH340 |
| `--interval` | 上报周期 ms（默认 50；**固件最低 20ms**，小于 20 会报错） |
| `--csv` | CSV 记录路径（可选） |

CSV 列：`t_s, enc1~4, tgt1~4, rpm1~4, rpm_raw1~4, sbus_frame_cnt, sbus_ok_cnt`（常规 56B 帧时 rpm_raw 列为空）。

## 代码结构

| 函数/类 | 作用 |
|---------|------|
| `crc8()` / `build_frame()` | CRC8 与组帧（自包含实现） |
| `parse_status()` | 0xF0 payload 解析，按长度自动区分 56B/72B |
| `enable_vt()` | Windows 控制台启用 ANSI 转义（失败自动退回单行刷新） |
| `StatusMonitor._reader()` | 后台读线程：帧解析 + 更新快照 + CSV 写入 |
| `StatusMonitor._render_table()` | 生成 4 通道实时表格 |
| `StatusMonitor.close()` | 停止线程 + UNSUBSCRIBE + 关闭串口/CSV |

## 用到的协议命令（二进制）

`0x40 SUBSCRIBE`（开启上报）、`0x41 UNSUBSCRIBE`（关闭上报）、`0xF0 STATUS_REPORT`（MCU 主动推送，56B/72B 两种 payload）。

## 常见问题

| 现象 | 处理 |
|------|------|
| 一直显示“等待第一帧” | 检查订阅 ACK 是否成功；确认 `--interval >= 20`；设备是否在线 |
| 表格乱跳/重叠 | 终端不支持 ANSI 时自动退回单行刷新；Windows 请用较新终端（Windows Terminal / VS Code） |
| 想显示 rpm_raw | 先发送 `0x44 DEBUG_SPEED=1`（如 `03_binary_protocol` 的 `drv.debug_speed(True)`），帧自动扩展为 72B，本工具自动识别 |
| 退出后固件仍在上报 | 本工具退出时已发送 UNSUBSCRIBE；若强杀进程导致未发出，重启工具或重发 0x41 即可 |
| CRC 校验失败频繁 | 波特率必须 2000000-8N1；确认 CRC8 范围为 CMD+LEN+DATA |

> 协议细节以 [`common/协议规范.md`](../../common/协议规范.md) 为准。
