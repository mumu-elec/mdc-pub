# ros1_motor_driver — Motor Driver Controller 的 ROS1 (Noetic) 驱动包

## 功能

针对 **Motor Driver Controller**（STM32F401 + TB6612 四路带编码器直流电机驱动器）的 ROS1 驱动节点。节点通过 **USB 虚拟串口（CH340N，固定 2000000-8N1）** 与固件通信，功能与 ROS2 版 `ros2_motor_driver` 完全对齐：

- 启动时自动发送文本指令 `/priority 1\n` 并等待回显，将 USB 置为控制主控（否则控制帧会被固件仲裁拒绝）；
- 发送 `SUBSCRIBE (0x40)` 开启状态周期上报，后台线程解析 **0xF0 STATUS_REPORT** 并发布 `/motor_status`；
- 订阅 `/motor_cmd`，组 **0x31 MOTOR_CTRL** 控制帧发送（内部按 30Hz 限流，建议发布频率 10~30Hz）；
- 三个服务：`get_config`（0x10 READ_PARAM）、`set_config`（0x11 WRITE_PARAM）、`save_config`（0x20 SAVE_EEPROM），通过 ACK 帧完成请求-应答；
- 节点关闭时自动 `UNSUBSCRIBE (0x41)` 并发送全零控制帧，使电机停车、关闭状态上报。

二进制帧格式、CRC8、命令号、STATUS_REPORT 布局**完全按** [`common/协议规范.md`](../../common/协议规范.md)（布局 v2.1，config_t = 231B）实现。

## 消息/服务定义

| 话题/服务 | 类型 | 说明 |
|-----------|------|------|
| `/motor_cmd` | `ros1_motor_driver/MotorCmd` | 订阅：四通道目标值 `int32[4] target`。含义随各通道控制模式：开环=PWM±1000 / 速度=RPM / 位置=0.1° |
| `/motor_status` | `ros1_motor_driver/MotorStatus` | 发布：`enc[4]` 编码器累计脉冲、`tgt[4]` 当前目标、`rpm[4]` 滤波后转速、`sbus_frame_cnt` / `sbus_ok_cnt`（含 `std_msgs/Header`） |
| `/get_config` | `ros1_motor_driver/GetConfig` | 服务：空请求 → 响应 `uint8[231] config`（config_t 全量 231B，小端） |
| `/set_config` | `ros1_motor_driver/SetConfig` | 服务：请求 `uint8[231] config` → 响应 `bool success`（仅 RAM，受保护字段自动还原） |
| `/save_config` | `ros1_motor_driver/SaveConfig` | 服务：空请求 → 响应 `bool success`（RAM 配置写入 EEPROM，约 190ms） |

## 硬件与环境要求

- **硬件**：Motor Driver Controller 主板，USB Type-C 连接电脑（CH340N 虚拟串口）
- **系统**：Ubuntu 20.04 + **ROS1 Noetic**（`ros-noetic-desktop-full`）
- **权限**：当前用户需能访问串口设备
  ```bash
  sudo usermod -aG dialout $USER    # 重新登录生效
  ls -l /dev/ttyUSB*                # 确认设备存在
  ```
- **波特率固定 2000000-8N1**，无需也不能修改（固件 USB 口固定）
- 实时控制前必须确认 `/priority 1` 已生效（驱动节点启动时自动发送并等待回显）

## 构建

```bash
# 1. 创建并进入工作区（假设已有 ~/catkin_ws）
mkdir -p ~/catkin_ws/src && cd ~/catkin_ws
# 2. 将本包复制到 src/ 下:
#    cp -r <例程>/ros/ros1_motor_driver src/
# 3. 构建（需先 source ROS1 环境）
source /opt/ros/noetic/setup.bash
catkin_make
source devel/setup.bash
```

> 也可使用 `catkin build`（catkin_tools）：`catkin build ros1_motor_driver`

## 运行

```bash
# 方式一: roslaunch（推荐, 参数可覆盖）
roslaunch ros1_motor_driver motor_driver.launch
roslaunch ros1_motor_driver motor_driver.launch port:=/dev/ttyUSB1 \
    status_interval_ms:=20

# 方式二: rosrun
rosrun ros1_motor_driver motor_driver_node \
    _port:=/dev/ttyUSB0 _baud:=2000000 _status_interval_ms:=50
```

启动日志应包含：

```
[ INFO] 串口 /dev/ttyUSB0 已打开 (baud=2000000, 8N1)
[ INFO] 已发送文本指令: /priority 1 (等待回显...)
[ INFO] /priority 1 回显: "OK (RAM only)" (USB 主控已就绪)
[ INFO] SUBSCRIBE(0x40) 成功: 状态上报间隔 50 ms
```

## 使用示例

**查看状态**（应与固件 0xF0 上报一致，默认 50ms 一帧）：

```bash
rostopic echo /motor_status
```

**手动发布控制指令**（10~30Hz 连续发布，开环 PWM 例：通道1 = +300）：

```bash
rostopic pub -r 20 /motor_cmd ros1_motor_driver/MotorCmd \
    "{target: [300, 0, 0, 0]}"
```

**查询配置**：

```bash
rosservice call /get_config "{}"
```

**保存配置**（set_config 后持久化）：

```bash
rosservice call /save_config "{}"
```

**键盘遥控**（另开一个终端）：

```bash
rosrun ros1_motor_driver teleop.py
# 按键: 1~4 选择通道, +/- 调整目标, 0 归零, q 退出
```

## 代码结构

```
ros1_motor_driver/
├── CMakeLists.txt            # catkin_package + add_message_files + generate_messages
├── package.xml               # 依赖: roscpp rospy std_msgs message_generation
├── msg/
│   ├── MotorCmd.msg          # 四通道目标值
│   └── MotorStatus.msg       # 状态上报
├── srv/
│   ├── GetConfig.srv         # 读 config_t (231B)
│   ├── SetConfig.srv         # 写 config_t
│   └── SaveConfig.srv        # 保存 EEPROM
├── src/
│   └── motor_driver_node.cpp # 节点: 串口(termios) + 帧解析 + 话题/服务
├── launch/
│   └── motor_driver.launch
├── scripts/
│   └── teleop.py             # 键盘遥控 (10Hz 发布)
└── README.md
```

## 常见问题

| 现象 | 处理 |
|------|------|
| `Cannot open /dev/ttyUSB0` | 检查设备节点 `ls /dev/ttyUSB*`、加入 `dialout` 组、确认 CH340 驱动 |
| 收到 `/priority 1` 回显超时 | 确认固件为 v1.2.0+（SW_MAJOR=2）、USB 线连接正常；串口波特率必须 2000000 |
| 控制帧无效/电机不动 | 确认日志中 `/priority 1` 回显成功（USB 主控）；检查 `/timeout` 未超时归零；协议识别（/detect）期间控制帧被拒绝，稍等重试 |
| `/motor_status` 无输出 | 确认 SUBSCRIBE 成功日志；`rostopic list` 查看话题是否注册；检查 `status_interval_ms` ≥ 20 |
| 发布 /motor_cmd 被限流 | 驱动节点按 30Hz 限流发送，日志有提示；建议发布频率 10~30Hz |
| CRC 校验失败/解析不到帧 | 固件与上位机协议版本 D 必须一致（布局 v2.1）；CRC8 范围为 CMD+LEN+DATA（不含 SYNC），多项式 0x07 初值 0 |
| 版本不匹配 | 上位机版本 vD.F 的 D 必须等于固件 SW_MAJOR（=2） |
