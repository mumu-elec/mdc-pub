cmake_minimum_required(VERSION 3.0.2)
project(ros1_motor_driver)

find_package(catkin REQUIRED COMPONENTS
  roscpp
  rospy
  std_msgs
  message_generation
)

# 消息
add_message_files(
  FILES
  MotorCmd.msg
  MotorStatus.msg
)

# 服务
add_service_files(
  FILES
  GetConfig.srv
  SetConfig.srv
  SaveConfig.srv
)

generate_messages(
  DEPENDENCIES std_msgs
)

catkin_package(
  CATKIN_DEPENDS roscpp std_msgs message_runtime
)

include_directories(${catkin_INCLUDE_DIRS})

# mdc_lib 通用调用库（header-only，命名空间 mdc，零第三方依赖）
# mdc_lib.hpp 已内置在本包 include/ 目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 驱动节点（串口 termios 收发 + mdc_lib 打包/解析 + 话题/服务）
add_executable(motor_driver_node src/motor_driver_node.cpp)
add_dependencies(motor_driver_node
  ${${PROJECT_NAME}_EXPORTED_TARGETS}
  ${catkin_EXPORTED_TARGETS}
)
target_link_libraries(motor_driver_node ${catkin_LIBRARIES})

install(TARGETS motor_driver_node
  DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})

install(DIRECTORY launch
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION})

# 键盘遥控节点（Python, 需 chmod +x）
install(PROGRAMS scripts/teleop.py
  DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})
