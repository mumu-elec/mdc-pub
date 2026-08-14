cmake_minimum_required(VERSION 3.8)
project(ros2_motor_driver)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(rosidl_default_generators REQUIRED)

# 生成消息/服务接口（话题 /motor_cmd /motor_status，服务 get/set/save_config）
rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/MotorCmd.msg"
  "msg/MotorStatus.msg"
  "srv/GetConfig.srv"
  "srv/SetConfig.srv"
  "srv/SaveConfig.srv"
  DEPENDENCIES std_msgs
)

# 驱动节点（串口 + 帧解析 + 话题/服务）
add_executable(motor_driver_node src/motor_driver_node.cpp)
ament_target_dependencies(motor_driver_node rclcpp std_msgs)
rosidl_target_interfaces(motor_driver_node ${PROJECT_NAME} "rosidl_typesupport_cpp")

install(TARGETS motor_driver_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY launch
  DESTINATION share/${PROJECT_NAME})

# 键盘遥控节点（Python）
install(PROGRAMS scripts/teleop.py
  DESTINATION lib/${PROJECT_NAME})

ament_package()
