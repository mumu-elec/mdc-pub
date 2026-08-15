# mdc_lib — C++17 宿主版实现（header-only，零依赖）

> **库名：** mdc_lib（Motor Driver Controller Library）
> **平台：** 宿主（PC / 树莓派 / Linux / Windows / macOS）C++17
> **协议依据：** [`../API.md`](../API.md)（统一 API 规范，权威）+ [`../例程/common/协议规范.md`](../../例程/common/协议规范.md)（布局 v2.1，config_t=231B）
> **特性：** 单头文件、命名空间 `mdc`、仅依赖标准库、UTF-8 中文注释、无硬件依赖（串口由用户实现）。

---

## 一、文件清单

| 文件 | 说明 |
|------|------|
| `mdc_lib.hpp` | header-only 全量实现（打包 + 解析 + 流式 Parser） |
| `test_mdc_lib.cpp` | 无硬件自测（覆盖 API.md §8 全部验证向量），`main()` 全 PASS 返回 0 |
| `README.md` | 本文档 |

## 二、功能与设计

库只做两件事：**① 打包**（指令 → 要发送的字节）、**② 解析**（收到的字节 → 结构体）。串口收发由用户自己实现：

```
┌─────────────┐   返回字节      ┌──────────────┐
│  mdc_lib    │ ────────────▶  │  用户的串口   │  ser.write() / WriteFile()
│  打包/解析   │ ◀────────────  │  (用户实现)   │
└─────────────┘   收到的字节    └──────────────┘
```

### 返回形态约定（与 API.md §2.2/§2.3 语义对齐，本实现选「返回容器」风格）

| 类别 | 本实现签名 | 说明 |
|------|-----------|------|
| 打包函数 | `std::vector<uint8_t>` | 返回整帧字节；参数非法（DATA 超 250B、ch 越界、pwm>1000、受保护区偏移等）**抛 `std::invalid_argument`** |
| 文本函数 | `std::string` | 含结尾 `'\n'`（UTF-8），可直接写入串口 |
| 解析函数 | `bool` + 输出参数（结构体引用） | 成功返回 `true` 并写入结果；payload 长度不符 / CRC 失败返回 `false`，输出参数保持不变（不抛异常） |
| 流式 Parser | `Parser::feed(uint8_t)` → `std::optional<std::pair<uint8_t, std::vector<uint8_t>>>` | 收齐一帧且 CRC 通过返回 `{cmd, payload}`，否则 `std::nullopt` |

> 与 C 版（`md_xxx(uint8_t* out, uint16_t cap)` 写缓冲 + 返回长度）参数顺序、校验规则完全一致，仅返回形态不同；与 Python 版（返回 bytes / 抛 ValueError）语义相同。

## 三、API 速览（与 API.md 一一对应）

### 底层

| 函数 | 对应 API.md | 说明 |
|------|:-----------:|------|
| `uint8_t crc8(const uint8_t*, size_t)` / `crc8(const std::vector<uint8_t>&)` | §3.1 | CRC8 多项式 0x07 初值 0，按位计算 |
| `std::vector<uint8_t> build_frame(uint8_t cmd, const std::vector<uint8_t>& data)` | §3.2 | 组帧 `[0xAA][CMD][LEN][DATA][CRC]`，CRC 范围 CMD+LEN+DATA |
| `Frame parse_frame(const uint8_t*, size_t)` / `parse_frame(const std::vector<uint8_t>&)` | §3.3 | `Frame{cmd, payload, valid}`；SYNC/CRC 任一失败 `valid=false` |
| `class Parser` | §3.4 | 流式解析器，见 §四 |

### 文本指令层（返回 `std::string`）

| 函数 | 输出示例 |
|------|---------|
| `std::string text_build(cmd)` / `text_build(cmd, args)` / `text_build(cmd, const std::string*)` | `"/mode 1 speed\n"` / `"/mode\n"`（args 缺省=读取模式） |
| `text_version()` / `text_help()` / `text_status()` / `text_check()` | `/version\n` 等 |
| `text_detect()` / `text_save()` / `text_load()` / `text_reset()` | `/detect\n` 等 |
| `text_enczero(uint8_t ch)` | `/enczero 1\n`（ch=1~4，越界抛异常） |
| `text_mode(uint8_t ch, const std::string* mode = nullptr)` | `"/mode 1\n"`（读取）/ `"/mode 1 speed\n"` |

其余指令（speedctrl/posctrl/cpr/inv/einv/posangle/filter/uart2/priority/timeout/smap/rmap/dmap/sbusparam/sbusrange）统一用 `text_build` 构造：

```cpp
text_build("/speedctrl", "1 0.5 0.02 0.01 500 800 10 0");
text_build("/uart2",     "115200 0 uart");
text_build("/sbusrange", "172 1811");
```

### 二进制命令层（15 个，返回整帧 `std::vector<uint8_t>`）

| 函数 | CMD | DATA |
|------|:---:|------|
| `bin_ping()` | 0x01 | 无 |
| `bin_read_param()` | 0x10 | 无 |
| `bin_write_param(const std::vector<uint8_t>&)`（须 231B，否则抛异常） | 0x11 | config_t 231B |
| `bin_write_param(const Config&)`（便捷重载，内部 pack_config） | 0x11 | config_t 231B |
| `bin_write_field(uint16_t field_id, const std::vector<uint8_t>& value)`（offset<12 抛异常） | 0x12 | `[field_id:2B LE][value]` |
| `bin_save()` / `bin_load()` / `bin_factory_reset()` | 0x20/0x21/0x22 | 无 |
| `bin_motor_raw(uint8_t ch, uint8_t dir, uint16_t pwm)`（ch 0~3、dir 0/1、pwm≤1000） | 0x30 | `[ch][dir][pwm:2B LE]` |
| `bin_motor_ctrl(int32_t t0, t1, t2, t3)` | 0x31 | `[4×int32 LE]` |
| `bin_subscribe(uint16_t interval_ms)` | 0x40 | `[interval_ms:2B LE]`（固件钳位 ≥20ms，库不拦截） |
| `bin_unsubscribe()` | 0x41 | 无 |
| `bin_debug_sbus(uint8_t enable)` / `bin_debug_speed(uint8_t enable)` | 0x43/0x44 | `[enable:1B]` |
| `bin_enter_bl()` / `bin_reboot()` | 0x52/0x53 | 无 |

### 解析层（返回 `bool` + 输出参数）

| 函数 | 输出类型 | 说明 |
|------|---------|------|
| `parse_ack(payload, Ack&)` | `struct Ack { cmd, err }` | 输入 ACK 帧 DATA 段 1 字节 err；`err=0` 成功，任何非 0 失败（`Ack::ok()` 便捷判断）。**注意：DATA 段不含命令号，`cmd` 恒为 0**，调用方从帧头获取 |
| `parse_status(payload, Status&)` | `struct Status` | 56B（常规）/ 72B（扩展）按 len 自动兼容，见 §五 |
| `parse_detect(payload, Detect&)` | `struct Detect { proto, inv, baud }` | 6B：`[proto][inv][baud:4B LE]`；proto 0=失败 1=SBUS 2=UART 3=ELRS |
| `parse_sbus(payload, std::array<uint16_t,16>&)` | 16×uint16 | 32B，LE |
| `parse_config(raw231, Config&)` | `struct Config` | 231B 全字段（含位域） |
| `pack_config(const Config&)` → `std::vector<uint8_t>` | 231B | 往返无损；受保护区 offset 0~10 置 0 |

## 四、流式解析器 `Parser`

滑动窗口逐字节扫描：找 `0xAA` → 读 CMD/LEN → 收齐 DATA+CRC → 校验。行为与 API.md §3.4 一致：

- **LEN > 250**：丢弃该 `0xAA` 重扫；
- **CRC 失败**：丢弃该 `0xAA` 继续向后找；
- **文本回显等噪声**：在 `0xAA` 之前的字节被当作噪声丢弃（文本行无法通过 0xAA 校验，自动跳过）；
- 缓冲上限由构造参数 `max_data` 控制（默认 250，对齐 `MD_PARSER_BUF=256`）。

```cpp
mdc::Parser parser;
// 串口收字节回调（伪代码）
for (uint8_t b : rx_bytes) {
    auto frame = parser.feed(b);
    if (frame) {
        uint8_t cmd = frame->first;                       // 0xF0 STATUS_REPORT 等
        const auto& payload = frame->second;
        if (cmd == mdc::MD_CMD_STATUS_REPORT) {
            mdc::Status st;
            if (mdc::parse_status(payload, st)) { /* 处理状态 */ }
        }
    }
}
```

## 五、`Status` 结构体与 56B/72B 兼容

```cpp
struct Status {
    std::array<int32_t, 4> enc{};      // @0   编码器累计脉冲
    std::array<float, 4>   tgt{};      // @16  当前目标值
    std::array<int32_t, 4> rpm{};      // @32  滤波后转速
    std::array<int32_t, 4> rpm_raw{};  // @48  滤波前原始 RPM（56B 模式恒 0）
    uint32_t sbus_frame_cnt;           // 56B:@48 / 72B:@64
    uint32_t sbus_ok_cnt;              // 56B:@52 / 72B:@68
    uint8_t  extended;                 // 1=72B 扩展模式
};
```

`parse_status` 按 payload 长度自动区分 56B / 72B；其它长度返回 `false`。

## 六、`Config` 结构与位域约定

字段与 API.md §6.5 `md_config_t` 一一对应（`std::array<int32_t,4>` 等容器化），解析/打包按协议规范 §5 偏移表：

- `control_mode` / `motor_invert`：每电机 2bit，`(byte >> (ch*2)) & 0x03`；
- `speed_pid_type` / `pos_pid_type` / `speed_filter_type`：每电机 4bit（u16 打包）；
- `sbus_channel` / `rc_dir_ch`：**存储值 0~15 = CH1~16，结构体里用 1~16**（解析 +1，打包 -1）；
- `rc_map_mode`（bit0-3 每电机 1bit）与 `rc_dir_en`（bit4-7 每电机 1bit）共用 `rc_map_mode` 字节；
- `speed_ctrl_params` / `pos_ctrl_params`：**按电机主序**（每电机 kp/ki/kd/ilim 4×float，与固件 `ctrl_params_t` 一致），解析为 `speed_kp[4]` 等四个数组；
- `pack_config` 时**受保护区 offset 0~10 全部置 0**（固件写入时自动还原受保护字段）；
- `parse_config ↔ pack_config` 往返无损（float 按位级 memcpy 转换）。

`MD_ENABLE_CONFIG` 宏（默认 1）可置 0 裁掉 config 全字段函数以省空间（对齐 API.md §6.5）。

## 七、编译示例

### g++（Linux / MinGW / MSYS2）

```bash
g++ -std=c++17 -Wall -Wextra -o test_mdc_lib test_mdc_lib.cpp
./test_mdc_lib        # 期望输出：PASS: 367, FAIL: 0
```

### MSVC（Visual Studio 开发者命令行）

```bat
cl /nologo /std:c++17 /EHsc /W4 /I. test_mdc_lib.cpp /Fe:test_mdc_lib.exe
test_mdc_lib.exe
```

集成到自己的工程：把 `mdc_lib.hpp` 拷入工程 include 目录，`#include "mdc_lib.hpp"` 即可，无需链接任何库。

## 八、串口接入示例（用户实现收发）

```cpp
#include "mdc_lib.hpp"
#include <cstdio>

// 伪代码：假设已有串口对象 ser，提供 write(bytes) 与 read_byte()（用户实现）

int main() {
    // ── 发送：打包 → 写入 ──
    std::vector<uint8_t> ping = mdc::bin_ping();          // AA 01 00 15
    ser.write(ping);

    ser.write(mdc::text_mode(1, "speed"));                // "/mode 1 speed\n"
    ser.write(mdc::bin_motor_ctrl(300, 0, 0, 0));         // 通道1 开环 PWM 300
    ser.write(mdc::bin_subscribe(50));                    // 50ms 周期上报

    // ── 接收：逐字节喂给流式解析器 ──
    mdc::Parser parser;
    while (ser.has_data()) {
        auto frame = parser.feed(ser.read_byte());
        if (!frame) continue;
        uint8_t cmd = frame->first;
        const auto& payload = frame->second;
        if (cmd == mdc::MD_CMD_STATUS_REPORT) {
            mdc::Status st;
            if (mdc::parse_status(payload, st)) {
                std::printf("enc1=%d rpm1=%d\n", st.enc[0], st.rpm[0]);
            }
        } else if (cmd == mdc::MD_CMD_READ_PARAM) {
            mdc::Config cfg;
            if (mdc::parse_config(payload, cfg)) { /* 使用配置 */ }
        }
    }
    return 0;
}
```

## 九、集成步骤

1. 拷贝 `mdc_lib.hpp` 到工程（单头文件，零依赖）；
2. `#include "mdc_lib.hpp"`，使用 `mdc::` 命名空间；
3. 发送：`ser.write(mdc::bin_xxx(...))` / `ser.write(mdc::text_xxx())`；
4. 接收：逐字节喂 `mdc::Parser::feed()`，或对完整 payload 直接调 `mdc::parse_xxx()`；
5. 实时控制前先发 `text_build("/priority", "1")`（USB 优先）与 `/timeout` 配置（协议规范 §1）。

## 十、验证

- `g++ -std=c++17 -Wall -Wextra` 编译**零警告**（g++ 15.1.0 验证）；
- 运行 `test_mdc_lib` **367 项断言全 PASS**（`main()` 返回 0）；
- 覆盖 API.md §8 全部验证向量：`crc8([0x01,0x00])=0x15`、`crc8("123456789")=0xF4`、`build_frame(0x01,b"")=AA 01 00 15`、`build_frame(0x40,[0x32,0x00])=AA 40 02 32 00 9E`、`motor_ctrl(100,-200,0,300)` DATA 段、parse_status 56B/72B 偏移、parse_config↔pack_config 往返、流式解析器（垃圾/坏 CRC/两帧连发/LEN>250/半帧/批量）。

## 十一、常见问题

| 问题 | 说明 |
|------|------|
| 与 C 版 API 签名不同？ | 参数顺序、校验规则一致，仅返回形态为容器风格（见 §二） |
| `parse_ack` 返回的 `cmd` 是 0？ | ACK 帧 DATA 段只有 1 字节 err，命令号需从帧头（`Parser::feed` 返回的 cmd 或 `parse_frame`）获取 |
| 订阅后收不到 0xF0？ | 需先 `bin_subscribe(≥20ms)` 开启上报；`bin_debug_speed(1)` 后上报帧扩展为 72B（parse_status 自动兼容） |
| 打包 0x31 控制帧电机不动？ | 检查 `/priority 1`（USB 优先）与 `/timeout` 心跳窗口（协议规范 §1） |
| 文本指令无响应？ | 确认发送内容含 `'\n'`（库已自动带） |
| float 精度问题？ | `parse_config`/`pack_config` 用位级 memcpy 转换，往返逐字节一致 |
