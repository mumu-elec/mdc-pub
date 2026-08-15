# mdc_lib — RP2040 C SDK 纯 C 库（无 pico-sdk 依赖）
#
# 用法（在 pico-sdk 工程中）：
#   1. 把本目录加入工程的 add_subdirectory()，或直接把 mdc_lib.c 加入 target_sources；
#   2. target_link_libraries(<your_app> PRIVATE mdc_lib)
#
# 本库不链接 pico_stdlib / hardware_uart，只做协议字节打包与解析；
# 串口收发由用户通过 hardware/uart.h 自行实现（示例见 README.md）。

add_library(mdc_lib STATIC mdc_lib.c)

target_include_directories(mdc_lib PUBLIC ${CMAKE_CURRENT_LIST_DIR})

# 核心代码为 C89 兼容风格，c_std_99 足以覆盖（也可用 c_std_11）
target_compile_features(mdc_lib PUBLIC c_std_99)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mdc_lib PRIVATE -Wall -Wextra)
endif()
