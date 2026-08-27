# 04 状态订阅监控（ESP32 + MicroPython，基于 mdc_lib）

## 功能
- `md_bin_subscribe(50)` 打包 **0x40 SUBSCRIBE** 帧发送，开启 50ms 周期状态上报，控制板随即周期推送 **0xF0 STATUS_REPORT**
- 收到字节逐字节喂给 `MDParser` 流式解析；0xF0 帧用 `md_parse_status()` 解析，按 payload 长度自动兼容两种模式：
  - **常规 56B**：`enc[4]i32 + tgt[4]f32 + rpm[4]i32 + sbus_frame_cnt u32 + sbus_ok_cnt u32`
  - **扩展 72B**：常规基础上追加 `rpm_raw[4]i32`（滤波前原始转速，需先发 0x44 DEBUG_SPEED 开启）
- 控制台每秒打印 4 通道 enc / rpm（扩展模式下附带 raw、SBUS 计数）
- 可选：检测到 SSD1306 OLED 时在屏上显示通道 1 的 RPM（无 OLED 自动跳过）

## 硬件接线（ESP32 UART2 ↔ 控制板 RC 口）

| ESP32 引脚 | 方向 | 控制板 RC 口 |
|-----------|:---:|------------|
| GPIO17（UART2 TX） | → | RC 信号（USART2 RX） |
| GPIO16（UART2 RX） | ← | RC 信号（USART2 TX） |
| GND | — | GND（**必须共地**） |

文字接线图：

```
控制板 RC 口 (USART2)            ESP32 (MicroPython)
┌───────────────────┐          ┌───────────────────┐
│  RC 信号 (RX)     │◄─────────│  GPIO17 (UART2 TX) │
│  RC 信号 (TX)     │─────────►│  GPIO16 (UART2 RX) │
│  GND              │──────────│  GND               │
└───────────────────┘          └───────────────────┘
```

### OLED 接线（可选，I2C，SSD1306 128×64）

| OLED | ESP32 引脚 |
|------|-----------|
| SCL | GPIO22（`OLED_SCL`） |
| SDA | GPIO21（`OLED_SDA`） |
| VCC | 3.3V |
| GND | GND |

> 需先把 MicroPython 的 `ssd1306.py` 驱动模块上传到 ESP32（Thonny/ampy/mpremote 均可）；未上传或未接线时例程自动跳过 OLED，不影响主功能。引脚可在 `main.py` 顶部 `OLED_SCL` / `OLED_SDA` / `OLED_ADDR` 修改。

## 控制板预配置
先用 USB 线连接控制板到电脑（USB 虚拟串口参数固定 **2000000-8N1**），在串口调试工具中发送：

```
/uart2 115200 0 uart
```

将 USART2 配置为 **UART 模式**（115200、极性正常、模式 uart），立即生效。

> 可选：要查看滤波前原始转速（72B 扩展上报），把 `main.py` 顶部 `ENABLE_EXTENDED = True`，例程会先发 `0x44 DEBUG_SPEED=1` 再订阅。

## 依赖与 mdc_lib
- 本例程只依赖 MicroPython 标准库（`machine` / `utime`，OLED 可选 `ssd1306` 模块）与 **mdc_lib**。
- `mdc_lib.py` 已随例程内置（本目录），上传整个例程文件夹到设备即可运行（`main.py` 直接 `import mdc_lib`）；如需更新库版本，用 `../../../../mdc_lib/esp32/micropython/mdc_lib.py` 覆盖。
- mdc_lib 是纯计算库（零依赖），只负责协议**打包/解析**；串口收发在例程里自己实现。

## mdc_lib 调用指南
本例程用到的 mdc_lib API：

| API | 说明 |
|-----|------|
| `md_bin_subscribe(interval_ms)` | 打包 0x40 SUBSCRIBE 帧（`[interval_ms:2B LE]`，固件钳位 ≥20ms） |
| `md_bin_debug_speed(enable)` | 打包 0x44 DEBUG_SPEED 帧：开启后 0xF0 帧为 72B 扩展模式 |
| `md_bin_unsubscribe()` | 打包 0x41 UNSUBSCRIBE 帧，关闭上报（退出时发送） |
| `MDParser.feed(byte)` | 流式解析：逐字节喂入，完整帧返回 `(cmd, payload)`，否则 `None`；自动找 `0xAA` 同步 + CRC8 校验 |
| `md_parse_status(payload)` | 解析 0xF0 帧 DATA 段 → `md_status_t(enc, tgt, rpm, rpm_raw, sbus_frame_cnt, sbus_ok_cnt, extended)`；56B/72B 自动兼容 |
| `MD_CMD_STATUS_REPORT` | 0xF0 命令字常量（用于帧过滤） |

实际调用示例（节选自 `main.py`）：

```python
import mdc_lib

# 开启 50ms 周期状态上报
uart.write(mdc_lib.md_bin_subscribe(50))

parser = mdc_lib.MDParser()
# 主循环：读 UART 字节 -> 喂 parser -> 0xF0 帧 -> md_parse_status 解析
for b in uart.read(n):
    r = parser.feed(b)                    # (cmd, payload) 或 None
    if r and r[0] == mdc_lib.MD_CMD_STATUS_REPORT:
        st = mdc_lib.md_parse_status(r[1])  # 56B/72B 自动兼容
        print("rpm:", tuple(st.rpm), "enc:", tuple(st.enc))

# 退出时关闭上报
uart.write(mdc_lib.md_bin_unsubscribe())
```

## 文件上传与运行（三选一）

### 方式一：Thonny（推荐）
1. 打开 Thonny → 选择解释器 MicroPython (ESP32) 及对应串口
2. 把 `mdc_lib.py`、`main.py`（可选 `ssd1306.py`）保存到设备（**同目录**）
3. 打开 `main.py` 点击运行 ▶；Ctrl+C 退出（退出时自动 0x41 UNSUBSCRIBE）

### 方式二：ampy（命令行）
```
ampy --port COM5 put mdc_lib.py
ampy --port COM5 put main.py
ampy --port COM5 reset
```

### 方式三：mpremote（命令行）
```
mpremote connect COM5 cp mdc_lib.py :
mpremote connect COM5 cp main.py :
mpremote connect COM5 reset
```

## 代码结构
- `main.py`：
  - 顶部可调参数：`SUBSCRIBE_MS` / `PRINT_INTERVAL_MS` / `ENABLE_EXTENDED` / `RX_TIMEOUT_MS` / OLED 引脚
  - `try_init_oled()`：try/except 包裹，无 SSD1306 自动跳过
  - `main()`：可选 DEBUG_SPEED → 订阅（`md_bin_subscribe`）→ 循环读字节喂 `MDParser` → 0xF0 帧用 `md_parse_status()` 解析 → 每秒打印 + 刷新 OLED → 退出时 `md_bin_unsubscribe()` 自动取消订阅

## 常见问题
| 现象 | 处理 |
|------|------|
| 订阅失败/超时 | 检查接线、共地；确认已 `/uart2 115200 0 uart`；波特率一致 |
| 一直"读帧超时"/无打印 | 确认 SUBSCRIBE 已成功；`SUBSCRIBE_MS` 不要小于 20ms；确认另一侧没有占用 RC 口 |
| 解析报"未知长度" | 确认 `ENABLE_EXTENDED` 与固件实际模式一致（56B 或 72B），`md_parse_status()` 只认这两种长度 |
| OLED 不显示 | 检查 I2C 接线与地址（默认 0x3C，部分屏为 0x3D）；确认已上传 `ssd1306.py` |
| 打印的行不刷新 | 本例程为每秒换行打印（非 \r 覆盖），属预期行为 |
| ImportError: no module named 'mdc_lib' | `mdc_lib.py` 未上传或不在 `main.py` 同目录 |
