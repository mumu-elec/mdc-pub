# 极简控制（mdc_lite）—— 只发控制帧，不看回包

> **分类：** mdc_lib/cpp/examples/极简控制　|　**库：** `mdc_lite`（send-only，极简调用库）
> **核心：** 上位机调参、下位机执行。你只要告诉下位机「发什么控制量」，本程序把**要发送的控制帧**打包好给你，串口收发由你实现。
> **不解析任何回包** —— 若需回读下位机转速，请用「控制+回调」例程（`mdc_lite_ctrl`）。

## 功能

- 只调用 `mdc_lite`（send-only），生成 4 种控制帧字节：`ctrl` / `stop` / `subscribe` / `unsubscribe`
- 打印每帧字节（HEX）与长度（`AA CMD LEN DATA CRC8` 完整帧、小端、CRC8 0x07 初值 0），可直接 `ser.write()`
- 无硬件即可运行（演示库打包，不依赖串口）

## API 速览

```cpp
mdc_lite::ctrl(m0, m1, m2, m3)      // 0x31 MOTOR_CTRL，四通道 int32 目标值 → std::vector<uint8_t>
mdc_lite::stop()                    // 0x31 便捷：ctrl(0,0,0,0) 全零帧（急停/退出）
mdc_lite::subscribe(interval_ms)    // 0x40 SUBSCRIBE，开启状态周期上报（建议 ≥20ms）
mdc_lite::unsubscribe()             // 0x41 UNSUBSCRIBE，关闭状态上报
```

一行示例：`ser.write(mdc_lite::ctrl(100, -200, 0, 300));`

## 串口接入示例（用户实现收发）

库只返回要发送的字节，串口发送由你完成：

```cpp
#include "mdc_lite.hpp"
#include <vector>

// 假设已有串口对象 ser，提供 write(bytes)（用户实现，见 完整/01_hello_serial 的 serial_port）
ser.write(mdc_lite::subscribe(50));            // 先订阅（若需回读速度的前提）
ser.write(mdc_lite::ctrl(100, -200, 0, 300));  // 四通道目标值
ser.write(mdc_lite::ctrl(0, 0, 0, 0));         // 归零
ser.write(mdc_lite::unsubscribe());            // 退出前取消订阅（可选的善后）
```

## 集成步骤

1. 拷贝 `mdc_lite.hpp`（独立实现，自带 CRC8/组帧，无需 mdc_lib）到你的工程（本目录已内置，开箱即用）；
2. `#include "mdc_lite.hpp"`，使用 `mdc_lite::` 命名空间；
3. 发送：`ser.write(mdc_lite::ctrl(...))` / `mdc_lite::subscribe(...)` 等，得到的就是要写的整帧字节；
4. 库不碰串口 —— 你负责 `ser.write()`（Windows/Linux 串口实现可参考 `完整/01_hello_serial` 的 `serial_port.h/.cpp`）。

## 与 mdc_lib 的关系

`mdc_lite` 是**独立实现**：自带 CRC8、组帧 `[0xAA][CMD][LEN][DATA][CRC8]`（LE，CRC8 0x07 初值 0），
`ctrl/stop/subscribe/unsubscribe` 直接生成要发送的整帧字节，**不依赖 `mdc_lib`**（不 include `mdc_lib.hpp`）。
本程序只用发送侧；需要回读转速时请引 `mdc_lite_ctrl.hpp`（见「控制+回调」）。

## 编译方法

```bash
g++ -std=c++17 -Wall -Wextra -o lite_ctrl main.cpp
./lite_ctrl        # 终端打印各帧字字节，无需硬件/串口
```

## 验证

- `subscribe(50)` 整帧 == `AA 40 02 32 00 9E`（CRC=0x9E）
- `ctrl(100,-200,0,300)` DATA == `64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00`
- 字节向量与 mdc_lib 打包结果逐字节一致（独立实现已自测，见 `mdc_lib/cpp/test_mdc_lite.cpp`）

> 协议细节以 [`../../../协议规范.md`](../../../协议规范.md) 为准。
