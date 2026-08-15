# ros2_motor_driver — Motor Driver Controller 的 ROS2 (Humble) 驱动包

## 功能

针对 **Motor Driver Controller**（STM32F401 + TB6612 四路带编码器直流电机驱动器）的 ROS2 驱动节点。节点通过 **USB 虚拟串口（CH340N，固定 2000000-8N1）** 与设备通信：

- 协议打包/解析全部由 **mdc_lib 通用调用库**完成（CRC8、组帧、滑动窗口解析、状态帧/配置解析均位于库内），节点只保留串口收发与 ROS 接口；
- 启动时用 `mdc::text_build("/priority", "1")` 发送文本指令 `/priority 1` 并等待回显，将 USB 置为控制主控（否则控制帧会被设备仲裁拒绝）；
- 发送 `SUBSCRIBE (0x40)` 开启状态周期上报，后台线程用 `mdc::Parser` 流式解析 **0xF0 STATUS_REPORT**（常规 56B / 扩展 72B 自动兼容）并发布 `/motor_status`；
- 订阅 `/motor_cmd`，用 `mdc::bin_motor_ctrl` 组 **0x31 MOTOR_CTRL** 控制帧发送（内部按 30Hz 限流，建议发布频率 10~30Hz）；
- 三个服务：`get_config`（0x10 READ_PARAM）、`set_config`（0x11 WRITE_PARAM）、`save_config`（0x20 SAVE_EEPROM），分别由 `mdc::bin_read_param` / `mdc::bin_write_param` / `mdc::bin_save` 打包，配置解析/重打包使用 `mdc::parse_config` / `mdc::pack_config`；
- 节点关闭时自动 `UNSUBSCRIBE (0x41)`（`mdc::bin_unsubscribe`）并发送全零控制帧（`mdc::bin_motor_ctrl(0,0,0,0)`），使电机停车、关闭状态上报。

**硬件与接线：**

- Motor Driver Controller 主板 ×1、四路带编码器直流电机 ×4、编码器信号线按主板丝印接入；
- 主板 USB Type-C 口连接电脑（CH340N 虚拟串口），供电即可，无需额外接线；
- 实时控制前必须确认 `/priority 1` 已生效（驱动节点启动时自动发送并等待回显）。

## 消息/服务定义

| 话题/服务 | 类型 | 说明 |
|-----------|------|------|
| `/motor_cmd` | `ros2_motor_driver/msg/MotorCmd` | 订阅：四通道目标值 `int32[4] target`。含义随各通道控制模式：开环=PWM±1000 / 速度=RPM / 位置=0.1° |
| `/motor_status` | `ros2_motor_driver/msg/MotorStatus` | 发布：`enc[4]` 编码器累计脉冲、`tgt[4]` 当前目标、`rpm[4]` 滤波后转速、`sbus_frame_cnt` / `sbus_ok_cnt`（含 `std_msgs/Header`） |
| `/get_config` | `ros2_motor_driver/srv/GetConfig` | 服务：空请求 → 响应 `uint8[231] config`（config_t 全量 231B，小端） |
| `/set_config` | `ros2_motor_driver/srv/SetConfig` | 服务：请求 `uint8[231] config` → 响应 `bool success`（仅 RAM，受保护字段自动还原） |
| `/save_config` | `ros2_motor_driver/srv/SaveConfig` | 服务：空请求 → 响应 `bool success`（RAM 配置写入 EEPROM，约 190ms） |

## 依赖与 mdc_lib

- **系统**：Ubuntu 22.04 + **ROS2 Humble**（`ros-humble-desktop`）
- **ROS 依赖**：`rclcpp`、`std_msgs`、`rosidl_default_generators`（见 `package.xml`）
- **mdc_lib**：`include/mdc_lib.hpp`（header-only、命名空间 `mdc`、零第三方依赖）。协议打包/解析全部由库完成，节点内不包含任何协议实现代码。

**mdc_lib.hpp 已随本包内置（`include/mdc_lib.hpp`），开箱即用**：`CMakeLists.txt` 的 include 路径已指向本包 `include/` 目录，无需任何拷贝或额外配置；如需更新库版本，用 `../../../mdc_lib/cpp/mdc_lib.hpp` 覆盖本包 `include/mdc_lib.hpp` 即可。

## mdc_lib 调用指南

本节点用到的 mdc API（全部来自 `mdc_lib/cpp/mdc_lib.hpp`，命名空间 `mdc`）：

| mdc API | 节点用途 |
|---------|---------|
| `mdc::text_build(cmd, args)` | 构造文本指令行（自动带结尾 `\n`）：启动时发送 `/priority 1` |
| `mdc::bin_subscribe(interval_ms)` | 打包 0x40 SUBSCRIBE 帧，开启状态周期上报 |
| `mdc::bin_unsubscribe()` | 打包 0x41 UNSUBSCRIBE 帧，关闭状态上报 |
| `mdc::bin_motor_ctrl(t0, t1, t2, t3)` | 打包 0x31 MOTOR_CTRL 控制帧（4×int32 LE） |
| `mdc::bin_read_param()` | 打包 0x10 READ_PARAM 帧，读取 config_t 231B |
| `mdc::bin_write_param(config)` | 打包 0x11 WRITE_PARAM 帧（config_t 231B） |
| `mdc::bin_save()` | 打包 0x20 SAVE_EEPROM 帧，RAM 配置写入 EEPROM |
| `mdc::parse_config(raw, cfg)` | 解析 231B config_t → `mdc::Config`（服务中校验/摘要） |
| `mdc::pack_config(cfg)` | `mdc::Config` → 231B config_t（受保护区 offset 0~10 置 0，设备写入时自动还原） |
| `mdc::Parser::feed(bytes, len)` | 流式解析：批量喂入字节，自动找 0xAA 同步 + CRC8 校验，文本噪声自动丢弃 |
| `mdc::parse_status(payload, st)` | 解析 0xF0 状态帧 → `mdc::Status`（56B / 72B 自动兼容） |
| `mdc::parse_ack(payload, ack)` | 解析 ACK 帧 DATA 段（1 字节 err，`err=0` 成功） |

调用示例（与节点内用法一致）：

```cpp
#include "mdc_lib.hpp"

// ① 发送文本指令（mdc_lib 自动补 '\n'）
const std::string line = mdc::text_build("/priority", "1");   // "/priority 1\n"
serial.write((const uint8_t*)line.data(), line.size());

// ② 发送二进制帧（库返回整帧，含 SYNC/CRC8，直接写串口）
serial.write(mdc::bin_subscribe(50));               // 0x40，50ms 周期上报
serial.write(mdc::bin_motor_ctrl(300, 0, 0, 0));    // 0x31，通道1 开环 PWM 300
serial.write(mdc::bin_read_param());                // 0x10 读全部配置

// ③ 接收：字节喂给流式解析器，库自动找同步字 + 校验 CRC8
mdc::Parser parser;
for (size_t i = 0; i < n; ++i) {
    auto frame = parser.feed(rx[i]);                // 完整帧 → {cmd, payload}
    if (frame && frame->first == mdc::MD_CMD_STATUS_REPORT) {
        mdc::Status st;
        if (mdc::parse_status(frame->second, st)) {
            // st.enc[0] 编码器脉冲 / st.rpm[0] 转速 / st.tgt[0] 目标值 ...
        }
    }
}
```

> 打包函数返回整帧字节（含 SYNC 与 CRC8），参数非法时抛 `std::invalid_argument`；
> 解析函数返回 `bool`，长度不符/CRC 失败返回 `false` 且输出参数不变。

## 构建

```bash
# 1. 创建并进入工作区（假设已有 ~/ros2_ws）
mkdir -p ~/ros2_ws/src && cd ~/ros2_ws
# 2. 将本包复制到 src/ 下（方式一仓库内构建时，保持 ../../../mdc_lib 相对路径有效）:
#    cp -r <例程>/ros/ros2_motor_driver src/
# 3. 构建（仅本包, 需先 source ROS2 环境）
source /opt/ros/humble/setup.bash
colcon build --packages-select ros2_motor_driver
source install/setup.bash
```

> 若同时构建多个包：`colcon build --packages-select ros2_motor_driver --symlink-install`
>
> 独立使用本包（mdc_lib.hpp 已复制到包内 include/）时无需额外操作，构建命令相同。

## 运行

```bash
# 方式一: launch（推荐, 参数可覆盖）
ros2 launch ros2_motor_driver motor_driver.launch.py
ros2 launch ros2_motor_driver motor_driver.launch.py \
    port:=/dev/ttyUSB1 status_interval_ms:=20

# 方式二: ros2 run
ros2 run ros2_motor_driver motor_driver_node --ros-args \
    -p port:=/dev/ttyUSB0 -p baud:=2000000 \
    -p status_interval_ms:=50 -p send_priority_cmd:=true
```

启动日志应包含：

```
[INFO] 串口 /dev/ttyUSB0 已打开 (baud=2000000, 8N1)
[INFO] 已发送文本指令: /priority 1 (等待回显...)
[INFO] /priority 1 回显: "OK (RAM only)" (USB 主控已就绪)
[INFO] SUBSCRIBE(0x40) 成功: 状态上报间隔 50 ms
```

## 使用示例

**查看状态**（应与设备 0xF0 上报一致，默认 50ms 一帧）：

```bash
ros2 topic echo /motor_status
```

**手动发布控制指令**（10~30Hz 连续发布，开环 PWM 例：通道1 = +300）：

```bash
ros2 topic pub -r 20 /motor_cmd ros2_motor_driver/msg/MotorCmd \
    "{target: [300, 0, 0, 0]}"
```

**查询配置**：

```bash
ros2 service call /get_config ros2_motor_driver/srv/GetConfig "{}"
```

**保存配置**（set_config 后持久化）：

```bash
ros2 service call /save_config ros2_motor_driver/srv/SaveConfig "{}"
```

**键盘遥控**（另开一个终端）：

```bash
ros2 run ros2_motor_driver teleop.py
# 按键: 1~4 选择通道, +/- 调整目标, 0 归零, q 退出
```

## 代码结构

```
ros2_motor_driver/
├── CMakeLists.txt            # ament_cmake + rosidl_generate_interfaces + mdc_lib include 路径
├── package.xml               # 依赖: rclcpp std_msgs rosidl_default_generators
├── msg/
│   ├── MotorCmd.msg          # 四通道目标值
│   └── MotorStatus.msg       # 状态上报
├── srv/
│   ├── GetConfig.srv         # 读 config_t (231B)
│   ├── SetConfig.srv         # 写 config_t
│   └── SaveConfig.srv        # 保存 EEPROM
├── src/
│   └── motor_driver_node.cpp # 节点: 串口(termios) + mdc_lib 打包/解析 + 话题/服务
├── launch/
│   └── motor_driver.launch.py
├── scripts/
│   └── teleop.py             # 键盘遥控 (10Hz 发布)
└── README.md
```

## 常见问题

| 现象 | 处理 |
|------|------|
| `Cannot open /dev/ttyUSB0` | 检查设备节点 `ls /dev/ttyUSB*`、加入 `dialout` 组、确认 CH340 驱动 |
| 收到 `/priority 1` 回显超时 | 确认设备固件 v1.2.0+（协议版本 D=2）、USB 线连接正常；串口波特率必须 2000000 |
| 控制帧无效/电机不动 | 确认日志中 `/priority 1` 回显成功（USB 主控）；检查 `/timeout` 未超时归零；协议识别（/detect）期间控制帧被拒绝，稍等重试 |
| `/motor_status` 无输出 | 确认 SUBSCRIBE 成功日志；`ros2 topic list` 查看话题是否注册；检查 `status_interval_ms` ≥ 20 |
| 发布 /motor_cmd 被限流 | 驱动节点按 30Hz 限流发送，日志有提示；建议发布频率 10~30Hz |
| 解析不到帧/状态异常 | 0xAA 同步、CRC8 校验与 56B/72B 状态解析已全部由 mdc_lib 内部完成，无需手动处理；确认 mdc_lib 与设备固件协议版本一致（布局 v2.1） |
| 编译找不到 mdc_lib.hpp | 确认 CMakeLists.txt 的 include 路径；独立使用时按「依赖与 mdc_lib」章节把 mdc_lib.hpp 复制到包内 include/ |
| 版本不匹配 | 设备固件协议版本 D 必须与 mdc_lib 的协议版本（布局 v2.1）一致 |
