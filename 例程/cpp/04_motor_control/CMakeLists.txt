cmake_minimum_required(VERSION 3.10)
project(motor_control CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# mdc_lib（协议打包/解析，header-only）：本仓库内直接引用
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../mdc_lib/cpp)

find_package(Threads REQUIRED)

add_executable(motor_control main.cpp serial_port.cpp)
target_link_libraries(motor_control PRIVATE Threads::Threads)
