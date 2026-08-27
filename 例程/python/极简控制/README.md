# 极简控制（Python）— 只管发送控制帧

> **核心思想：上位机调参、下位机执行。** 本程序是 `mdc_lite`（send-only）的最小使用示例：
> 只关心「要发送什么控制量」并拿到要发送的帧，**不做任何接收解析**。

## 功能

- 用 `mdc_lite` 打包并发送 4 条命令：`ctrl`（0x31 四通道控制）、`stop`（全零急停）、
  `subscribe`（0x40 订阅上报）、`unsubscribe`（0x41 取消订阅）。
- 默认在终端打印每帧字节（无需硬件/串口即可查看）；也可 `--port COM5` 真正发送。

## 依赖与 mdc_lite

- 串口收发由本程序实现；协议打包由 **mdc_lite**（send-only）完成。
- 库已随例程内置（本目录 `mdc_lite.py` + `mdc_lib.py`），开箱即用：
  ```python
  import mdc_lite
  frame = mdc_lite.ctrl(100, -200, 0, 300)     # 0x31 四通道目标值
  ser.write(frame)
  ```
- 如需更新库版本，用 `../../../../mdc_lib/python/mdc_lite.py` 覆盖本目录文件（mdc_lib 同理）。

## 极简 API 速览

| 函数 | 命令 | 说明 |
|------|:---:|------|
| `mdc_lite.ctrl(m0,m1,m2,m3)` | 0x31 | 四通道目标值（open=PWM±1000 / speed=RPM / pos=0.1°） |
| `mdc_lite.stop()` | 0x31 | 全零急停帧 |
| `mdc_lite.subscribe(ms)` | 0x40 | 订阅状态上报（ms≥20） |
| `mdc_lite.unsubscribe()` | 0x41 | 取消订阅 |

## 使用

```bash
python main.py                 # 打印各帧（无需硬件）
python main.py --port COM5     # 连接下位机实际发送
```

## 与 mdc_lib 的关系

`mdc_lite` 是 `mdc_lib` 之上的一层**极薄封装**（见 [`../../../../mdc_lib/LITE.md`](../../../../mdc_lib/LITE.md)），
字节布局/CRC8/帧格式与 `mdc_lib` 完全一致，仅把关注范围收窄到「发控制帧」。若需回读转速，见**控制+回调**例程。
