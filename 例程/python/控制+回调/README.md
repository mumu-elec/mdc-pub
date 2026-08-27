# 控制+回调（Python）— 发控制帧 + 接收速度回调

> **核心思想：上位机调参、下位机执行。** 本程序是 `mdc_lite_ctrl`（调用+回调接收）的最小使用示例：
> 一边发 `0x31` 控制帧，一边用注册的**速度回调**实时打印由下位机推送的四通道转速 `rpm`。

## 功能

- 用 `mdc_lite_ctrl.MDLite(on_speed)` 注册速度回调。
- 订阅 `0x40` 开启 `0xF0` 状态上报 → 边发 `0x31` 控制帧、边把收到的字节喂给 `feed()`；
  遇到 `0xF0` 时自动解析并调用 `on_speed(rpm)`（rpm 为四通道转速元组）。
- 退出自动发全零急停帧并 `unsubscribe`。
- `--demo` 离线演示（喂一帧合成 `0xF0`，无需硬件）。

## 依赖与 mdc_lite_ctrl

- 串口收发由本程序实现；打包/解析由 **mdc_lite_ctrl**（调用+回调接收）完成。
- 库已随例程内置（本目录 `mdc_lite_ctrl.py` + `mdc_lite.py` + `mdc_lib.py`），开箱即用：
  ```python
  from mdc_lite_ctrl import MDLite
  def on_speed(rpm): print("实时 rpm:", rpm)
  mdc = MDLite(on_speed)
  ser.write(mdc.subscribe(50))
  while True:
      ser.write(mdc.ctrl(100, -200, 0, 300))
      for b in ser.read(64): mdc.feed(b)     # 0xF0 到达时自动回调 on_speed
  ```
- 如需更新库版本，用 `../../../../mdc_lib/python/mdc_lite_ctrl.py` 覆盖本目录文件（mdc_lite/mdc_lib 同理）。

## 极简 API 速览

| 方法 | 说明 |
|------|------|
| `MDLite(on_speed)` | 构造并注册速度回调 `on_speed(rpm0,rpm1,rpm2,rpm3)` |
| `.ctrl(m0,m1,m2,m3)` / `.stop()` | 0x31 控制帧 / 全零急停帧 |
| `.subscribe(ms)` / `.unsubscribe()` | 0x40 订阅 / 0x41 取消订阅 |
| `.feed(byte)` | 逐字节喂入，遇 `0xF0` 自动回调 |
| `.reset()` | 清空流式解析缓冲 |

## 使用

```bash
python main.py --demo            # 离线演示（无需硬件）
python main.py --port COM5       # 连真机：订阅+发控制+回调打印 rpm
python main.py --port COM5 --target "0 300 0 0"
```

## 与 mdc_lib 的关系

`mdc_lite_ctrl` 在 `mdc_lite`（发送侧）基础上增加**流式回调接收器**（见
[`../../../../mdc_lib/LITE.md`](../../../../mdc_lib/LITE.md)），只解析 `0xF0` 并回调四通道 rpm，
其余帧/噪声一律忽略。
