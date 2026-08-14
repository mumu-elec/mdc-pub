# =====================================================================
# motor_driver.launch.py — ROS2 (Humble) 驱动节点 launch
# 用法:
#   ros2 launch ros2_motor_driver motor_driver.launch.py
#   ros2 launch ros2_motor_driver motor_driver.launch.py port:=/dev/ttyUSB1 \
#       status_interval_ms:=20 send_priority_cmd:=true
# =====================================================================
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    port_arg = DeclareLaunchArgument(
        'port', default_value='/dev/ttyUSB0',
        description='USB 虚拟串口设备 (CH340N, 固定 2000000-8N1)')

    interval_arg = DeclareLaunchArgument(
        'status_interval_ms', default_value='50',
        description='STATUS_REPORT 上报间隔 (ms, 固件下限 20)')

    priority_arg = DeclareLaunchArgument(
        'send_priority_cmd', default_value='true',
        description='启动时发送文本指令 /priority 1, 使 USB 成为控制主控')

    node = Node(
        package='ros2_motor_driver',
        executable='motor_driver_node',
        name='motor_driver',
        output='screen',
        parameters=[{
            'port': LaunchConfiguration('port'),
            'baud': 2000000,                      # 固件 USB 口固定波特率
            'status_interval_ms': LaunchConfiguration('status_interval_ms'),
            'send_priority_cmd': LaunchConfiguration('send_priority_cmd'),
        }],
        # 如需修改波特率: ros2 run ros2_motor_driver motor_driver_node \
        #   --ros-args -p baud:=115200
    )

    return LaunchDescription([port_arg, interval_arg, priority_arg, node])
