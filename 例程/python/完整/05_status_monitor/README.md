# 05_status_monitor — 状态监控

## 功能

- 用 `md_bin_subscribe(interval)` 打包并发送 `0x40 SUBSCRIBE`，开启 STATUS_REPORT（0xF0）周期上报（`--interval`，默认 50ms；固件最低 20ms）。
- 后台读线程把每个字节喂给 `MDParser` 流式解析器（自动找 `0xAA` 同步 + CRC8 校验），解析出 `cmd==0xF0` 的 payload 后用 `md_parse_status` 解析，**自动兼容两种 payload**：
  - 常规 **56B**：`enc[4]i32 + tgt[4]f32 + rpm[4]i32 + sbus_frame_cnt:u32 + sbus_ok_cnt:u32`；
  - 扩展 **72B**：上述基础上追加 `rpm_raw[4]i32`（需先 `0x44 DEBUG_SPEED=1` 开启）；
- 控制台实时表格：ANSI 清屏刷新（不支持时自动退回 `\r` 单行覆盖），显示 4 通道 `enc / tgt / rpm`（扩展模式下附加 rpm_raw）。
- 可选 `--csv`：每收到一帧追加一行记录（含时间戳与全部字段），便于离线分析。
- Ctrl+C 干净退出：停止读线程 → 发送 `md_bin_unsubscribe()` → 关闭串口。

## 硬件与环境要求

| 项目 | 要求 |
|------|------|
| 硬件 | Motor Driver Controller（固件 v1.2.0+），USB Type-C 数据线 |
| 驱动 | CH340N 虚拟串口驱动 |
| 接线 | USB 线连接设备与电脑；设备需上电 |
| 系统 | Windows / Linux / macOS 均可 |
| Python | 3.8+ |
| 依赖 | pyserial（唯一依赖） |

## 依赖与 mdc_lib

```bash
pip install pyserial        # 或 pip install -r requirements.txt
```

- **mdc_lib**：订阅/退订帧的打包与 STATUS_REPORT 的解析统一由通用调用库完成（`md_bin_subscribe` / `md_bin_unsubscribe` / `MDParser` / `md_parse_status`），本脚本只负责串口收发、读线程与表格显示。
  mdc_lib 已随例程内置（本目录 `mdc_lib.py`），开箱即用，直接 `import mdc_lib` 即可；如需更新库版本，用 `../../../../mdc_lib/python/mdc_lib.py` 覆盖本目录文件。

## mdc_lib 调用指南

本例程用到的 mdc_lib API（详见 [`mdc_lib/API.md`](../../../../mdc_lib/API.md) §5/§6）：

| API | 返回 | 说明 |
|-----|------|------|
| `md_bin_subscribe(interval_ms)` | 整帧 bytes | 打包 0x40 SUBSCRIBE（固件钳位 ≥20ms） |
| `md_bin_unsubscribe()` | 整帧 bytes | 打包 0x41 UNSUBSCRIBE |
| `MDParser()` / `parser.feed(byte)` | `(cmd, payload)` 或 `None` | 逐字节流式解析，自动同步 + CRC8 校验 |
| `md_parse_status(payload)` | `md_status_t` | 解析 0xF0 payload，**56B/72B 自动兼容**；字段：enc/tgt/rpm/rpm_raw/sbus_frame_cnt/sbus_ok_cnt/extended |

实际调用示例（串口收发由用户侧实现）：

```python
import mdc_lib, serial

ser = serial.Serial("COM5", 2000000)
ser.write(mdc_lib.md_bin_subscribe(50))     # 开启 50ms 周期上报

parser = mdc_lib.MDParser()
for b in ser.read(4096):                    # 后台读线程：逐字节喂给解析器
    r = parser.feed(b)
    if r and r[0] == 0xF0:                  # STATUS_REPORT
        st = mdc_lib.md_parse_status(r[1])  # 56B 常规 / 72B 扩展自动兼容
        print(st.enc, st.rpm, st.sbus_ok_cnt)

ser.write(mdc_lib.md_bin_unsubscribe())     # 退出前关闭上报
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
| `StatusMonitor._wait_ack()` | 订阅/退订后等待 ACK（跳过其它帧） |
| `StatusMonitor._reader()` | 后台读线程：字节喂 `MDParser` → `md_parse_status` → 更新快照 + CSV 写入 |
| `StatusMonitor._render_table()` | 生成 4 通道实时表格（使用 md_status_t 字段） |
| `enable_vt()` | Windows 控制台启用 ANSI 转义（失败自动退回单行刷新） |
| `StatusMonitor.close()` | 停止线程 + UNSUBSCRIBE + 关闭串口/CSV |

## 常见问题

| 现象 | 处理 |
|------|------|
| 一直显示“等待第一帧” | 检查订阅 ACK 是否成功；确认 `--interval >= 20`；设备是否在线 |
| 表格乱跳/重叠 | 终端不支持 ANSI 时自动退回单行刷新；Windows 请用较新终端（Windows Terminal / VS Code） |
| 想显示 rpm_raw | 先发送 `0x44 DEBUG_SPEED=1`（如 `md_bin_debug_speed(True)`），帧自动扩展为 72B，本工具自动识别 |
| 退出后固件仍在上报 | 本工具退出时已发送 UNSUBSCRIBE；若强杀进程导致未发出，重启工具或重发 0x41 即可 |
| 解析不到帧 | 波特率必须 2000000-8N1；若之前有过异常流量，可调用 `parser.reset()` 清缓冲 |

> 协议细节以 [`common/协议规范.md`](../../../common/协议规范.md) 为准。
