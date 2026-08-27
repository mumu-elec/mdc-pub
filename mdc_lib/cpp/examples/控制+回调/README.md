# 控制+回调（mdc_lite_ctrl）—— 发控制帧 + 收速度回调

> **分类：** mdc_lib/cpp/examples/控制+回调　|　**库：** `mdc_lite_ctrl`（调用+回调接收，极简调用库）
> **核心：** 上位机调参、下位机执行。在 `mdc_lite`（send-only）基础上，本程序**流式接收**下位机主动推送的 `0xF0 STATUS_REPORT`，自动解析**四通道转速 rpm** 并回调给你的 `on_speed`。
> **串口收发由你实现**：库只返回要发送的字节、并解析你喂入的字节，不碰串口。

## 功能

- `mdc_lite_ctrl::MdLite` 同时提供：**发送侧**（复用 `mdc_lite`：`control`/`stop`/`subscribe`/`unsubscribe`）与**接收侧**（`feed`/`reset`）
- 注册 `on_speed(std::array<int32_t,4>)` 速度回调；`feed()` 遇到完整且 CRC 通过的 `0xF0`（56B/72B 自动兼容）时解析 rpm 并回调
- 无硬件即可运行：打印发送帧 + **模拟下位机上报**一帧 56B `0xF0` 喂入 `feed()`，展示回调被触发

## API 速览

```cpp
mdc_lite::MdLite mdc(on_speed);          // 构造：注册速度回调 on_speed(rpm[4])

// 发送侧（委托 mdc_lite）
mdc.control(m0, m1, m2, m3)               // 0x31 MOTOR_CTRL → std::vector<uint8_t>
mdc.ctrl(m0, m1, m2, m3)                  // 别名（与 mdc_lite::ctrl 同名）
mdc.stop()                                // 0x31 全零帧（急停/退出）
mdc.subscribe(interval_ms)                // 0x40 SUBSCRIBE（建议 ≥20ms）
mdc.unsubscribe()                         // 0x41 UNSUBSCRIBE

// 接收侧（回调）
mdc.feed(byte)                            // 逐字节喂入；0xF0 完整帧到达时回调 on_speed
mdc.reset()                               // 清空流式解析器（切换连接/重新同步）
```

一行示例：`mdc.control(100, -200, 0, 300)` 得到要发的帧；`for (uint8_t b : rx) mdc.feed(b);` 触发 `on_speed(rpm)`。

## 串口接入示例（用户实现收发，含回调）

```cpp
#include "mdc_lite_ctrl.hpp"
#include <cstdio>

void on_speed(const std::array<int32_t, 4>& rpm) {   // 速度回调
    std::printf("实时转速 rpm: %d %d %d %d\n", rpm[0], rpm[1], rpm[2], rpm[3]);
}

mdc_lite::MdLite mdc(on_speed);                       // 注册速度回调
ser.write(mdc.subscribe(50));                         // 先订阅（收速度的前提）

while (ser.has_data()) {
    ser.write(mdc.control(100, -200, 0, 300));        // 发送控制帧
    for (uint8_t b : ser.read(64)) mdc.feed(b);       // 0xF0 到达时自动回调 on_speed
}
```

## 集成步骤

1. 拷贝 `mdc_lite_ctrl.hpp`（连同 `mdc_lite.hpp`、`mdc_lib.hpp`）到你的工程（本目录已内置，开箱即用）；
2. `#include "mdc_lite_ctrl.hpp"`，使用 `mdc_lite::MdLite`；
3. **发送**：`ser.write(mdc.control(...))` / `mdc.subscribe(50)`，得到的就是要写的整帧字节；
4. **接收**：串口收到的每个字节调 `mdc.feed(b)`，`0xF0` 完整帧到达时库自动解析并回调 `on_speed`；
5. 切换连接 / 重新同步时调 `mdc.reset()` 清空解析器缓冲。

## 与 mdc_lib 的关系

`mdc_lite_ctrl` **独立实现**：发送侧复用同族独立极简库 `mdc_lite`，接收侧**自带** 0xF0 流式解析器（找 0xAA 同步 + CRC 校验 + 从 DATA 提取四通道 rpm），**不依赖 `mdc_lib`**，也不改变协议字节布局（LE / CRC8 0x07 初值0 / `[AA][CMD][LEN][DATA][CRC8]`）。它是「send-only」之上多加了「0xF0→速度回调」的接收侧。若一个工程只需发送，可只引 `mdc_lite.hpp`（见「极简控制」）；需回读转速时引 `mdc_lite_ctrl.hpp` 即可同时获得发送与回调。

## 编译方法

```bash
g++ -std=c++17 -Wall -Wextra -o lite_ctrl_cb main.cpp
./lite_ctrl_cb     # 终端打印帧字节 + 模拟 0xF0 回调，无需硬件/串口
```

## 验证

- `control(100,-200,0,300)` 帧头 `AA 31 10`，DATA 段与 mdc_lib 一致
- 喂入 56B `0xF0` 状态帧触发一次回调，`rpm == (1234, -567, 0, 9000)`
- 非 `0xF0` 帧（如 0x31 控制帧）/ 噪声 / 坏 CRC 不触发回调（库自测，见 `mdc_lib/cpp/test_mdc_lite.cpp`）

> 协议细节以 [`../../../协议规范.md`](../../../协议规范.md) 为准。
