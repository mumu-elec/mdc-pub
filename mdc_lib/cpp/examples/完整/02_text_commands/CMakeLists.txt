cmake_minimum_required(VERSION 3.10)
project(text_commands CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# mdc_lib.hpp 已内置在本目录，无需额外 include 路径

add_executable(text_commands main.cpp serial_port.cpp)
