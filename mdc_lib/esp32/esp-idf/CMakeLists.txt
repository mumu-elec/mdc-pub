# mdc_lib + mdc_lite — ESP32 ESP-IDF 组件（纯 C，零 IDF 依赖）
#
# 本组件只做协议字节打包与解析，不链接 driver/uart 等 IDF 组件；
# 串口收发由用户在自己的 app 里通过 driver/uart.h 实现（示例见 README.md）。
# mdc_lite.c / mdc_lite_ctrl.c 为极简封装（详见 LITE.md），同一组件一并打包。
#
# 用法：
#   1. 把本目录放到工程的 components/ 下（components/mdc_lib/），
#      idf.py 会自动识别该组件；
#   2. 在工程 CMakeLists.txt 里 REQUIRES mdc_lib（或通过依赖自动传递）；
#   3. #include "mdc_lib.h" 即可使用。

idf_component_register(
    SRCS "mdc_lib.c" "mdc_lite.c" "mdc_lite_ctrl.c"
    INCLUDE_DIRS "."
)
