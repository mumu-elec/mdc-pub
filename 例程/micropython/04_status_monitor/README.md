# 04 状态订阅监控（ESP32 + MicroPython）

## 功能
- 发送 **0x40 SUBSCRIBE**（50ms 周期）开启状态上报，控制板随即周期推送 **0xF0 STATUS_REPORT**
- 解析 0xF0：按 payload 长度自动兼容两种模式
  - **常规 56B**：`enc[4]i32 + tgt[4]f32 + rpm[4]i32 + sbus_frame_cnt u32 + sbus_ok_cnt u32`
  - **扩展 72B**：常规基础上追加 `rpm_raw[4]i32`（滤波前原始转速，需先发 0x44 DEBUG_SPEED 开启）
- 控制台每秒打印 4 通道 enc / rpm（可选打印 tgt、rpm_raw、SBUS 计数）
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

## 控制板预配置（必须）
先用 USB 线连接控制板（串口参数 **2000000-8N1**），发送：

```
/uart2 115200 0 uart
```

将 USART2 配置为 **UART 模式**（115200、极性正常、模式 uart），立即生效。

> 可选：要查看滤波前原始转速（72B 扩展上报），把 `main.py` 顶部 `ENABLE_EXTENDED = True`，例程会先发 `0x44 DEBUG_SPEED=1` 再订阅。

## 文件上传与运行（三选一）

### 方式一：Thonny（推荐）
1. 打开 Thonny → 选择解释器 MicroPython (ESP32) 及对应串口
2. 把 `motor_driver.py`、`main.py`（可选 `ssd1306.py`）保存到设备
3. 打开 `main.py` 点击运行 ▶；Ctrl+C 退出（退出时自动 0x41 UNSUBSCRIBE）

### 方式二：ampy（命令行）
```
ampy --port COM5 put motor_driver.py
ampy --port COM5 put main.py
ampy --port COM5 reset
```

### 方式三：mpremote（命令行）
```
mpremote connect COM5 cp motor_driver.py :
mpremote connect COM5 cp main.py :
mpremote connect COM5 reset
```

## 代码结构
- `motor_driver.py`：二进制协议封装库（与 02 例程同源，自包含，直接复制），含 `parse_status_report()` 与 `subscribe()/unsubscribe()/debug_speed()`
- `main.py`：
  - 顶部可调参数：`SUBSCRIBE_MS` / `PRINT_INTERVAL_MS` / `ENABLE_EXTENDED` / `RX_TIMEOUT_MS` / OLED 引脚
  - `try_init_oled()`：try/except 包裹，无 SSD1306 自动跳过
  - `main()`：订阅 → 循环读帧 → 按 cmd 过滤 → `parse_status_report()` 解析 → 每秒打印 + 刷新 OLED → 退出时自动取消订阅

## 用到的协议命令
| CMD | 名称 | 说明 |
|:---:|------|------|
| 0x40 | SUBSCRIBE | 开启状态周期上报（`[interval_ms:2B LE]`，最低 20ms） |
| 0x41 | UNSUBSCRIBE | 关闭状态上报（退出时自动执行） |
| 0x44 | DEBUG_SPEED | 可选：开启 72B 扩展上报（`ENABLE_EXTENDED = True` 时发送） |
| 0xF0 | STATUS_REPORT | 控制板主动推送：56B 常规 / 72B 扩展 |

## 常见问题
| 现象 | 处理 |
|------|------|
| 订阅失败/超时 | 检查接线、共地；确认已 `/uart2 115200 0 uart`；波特率一致 |
| 一直"读帧超时" | 确认 SUBSCRIBE 已成功；`SUBSCRIBE_MS` 不要小于 20ms；确认另一侧没有占用 RC 口 |
| 解析报"未知长度" | 确认 `ENABLE_EXTENDED` 与固件实际模式一致（56B 或 72B），`parse_status_report` 只认这两种长度 |
| OLED 不显示 | 检查 I2C 接线与地址（默认 0x3C，部分屏为 0x3D）；确认已上传 `ssd1306.py` |
| 打印的行不刷新 | 本例程为每秒换行打印（非 \r 覆盖），属预期行为 |
