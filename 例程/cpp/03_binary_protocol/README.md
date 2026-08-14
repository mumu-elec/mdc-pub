# 03_binary_protocol — 二进制帧协议类库与演示

## 功能

- **`motor_driver.hpp`**：`MotorDriver` 类（头文件实现，直接 `#include` 即可复用），封装二进制帧协议全部 18 条命令：
  - `crc8()` 静态方法：多项式 0x07、初值 0，计算范围 = CMD+LEN+DATA（不含 SYNC）
  - `buildFrame()` 组帧 / `sendFrame()` 发送 / `readFrame()` 滑动窗口收帧（找 0xAA → 按 LEN 收齐 → CRC 校验）
  - `readAck()` 校验 ACK（CMD 一致 + LEN==1 + err∈{0x00, 0xFF}）
  - 命令封装：`ping / read_param / write_param / write_field / save / load / factory_reset / motor_raw / motor_ctrl / subscribe / unsubscribe / debug_sbus / debug_speed / reboot`
- **`main.cpp`** 演示流程：打开串口 → PING（打印 ACK 结果）→ READ_PARAM（打印 config_t 前 16 字节 HEX + 解析头部 magic/版本字段 offset 0~10 + crc 校验）

## 硬件与环境要求

- Motor Driver Controller + USB Type-C，CH340 驱动，**2000000-8N1**
- 固件 **v1.2.0+**（协议版本 D=2，config_t=231B），与上位机协议版本一致
- C++17 编译器，零第三方依赖

## 编译方法

```bat
:: Windows MSVC
cl /std:c++17 /EHsc main.cpp serial_port.cpp /Fe:binary_protocol.exe

:: Windows MinGW / MSYS2
g++ -std=c++17 -O2 main.cpp serial_port.cpp -o binary_protocol.exe
```

```bash
# Linux g++ / CMake
g++ -std=c++17 -O2 main.cpp serial_port.cpp -o binary_protocol
cmake -B build && cmake --build build
```

## 运行方法

```bash
binary_protocol COM3          # Windows
binary_protocol /dev/ttyUSB0  # Linux
```

预期输出（示例）：

```
== 已打开 COM3 @ 2000000-8N1 ==
== PING (0x01) ==
  OK: 设备在线，ACK err=0x00
== READ_PARAM (0x10) ==
  收到 231 字节 (期望 231)
  前 16 字节 HEX: 00 52 44 4D 01 00 01 02 01 00 3A ...
== 头部字段解析 (offset 0~10) ==
  magic      [0-3]  = 0x4D445200 (正确)
  hw_ver     [4-6]  = 1.0.1
  sw_ver     [7-8]  = 2.1
  ...
```

## 代码结构

| 文件 | 说明 |
|------|------|
| `motor_driver.hpp` | `MotorDriver` 类：CRC8 / 组帧 / 收帧 / ACK 校验 / 全部命令封装（头文件实现） |
| `serial_port.h/.cpp` | 跨平台串口封装（与 01 相同，本目录自带） |
| `main.cpp` | 演示：PING + READ_PARAM 解析 |
| `CMakeLists.txt` | C++17，无外部依赖 |

## 用到的协议命令

| CMD | 名称 | DATA | 说明 |
|:---:|------|------|------|
| `0x01` | PING | 无 | 连通性测试，应答 ACK |
| `0x10` | READ_PARAM | 无 | 读取全部配置（应答 config_t 231B） |

（类库还封装了 `0x11/0x12/0x20/0x21/0x22/0x30/0x31/0x40/0x41/0x43/0x44/0x52/0x53` 等命令，可直接调用。）

## 常见问题

| 现象 | 处理 |
|------|------|
| PING 失败 | 确认波特率 2000000-8N1；确认没有其它程序占用串口 |
| CRC 校验失败 | 确认 CRC8 计算范围是 CMD+LEN+DATA（不含 SYNC），多项式 0x07 初值 0（本类已正确实现） |
| READ_PARAM 返回长度不符 | 固件版本过低（< v1.2.0），config_t 不是 231B |
| 版本不匹配 | 固件 SW_MAJOR 必须与上位机协议版本 D 一致 |

> 协议细节以 [`common/协议规范.md`](../../common/协议规范.md) 为准。
