// ============================================================================
// mdc_lite.hpp — 极简调用库（只管调用 / send-only，C++17 宿主版）
// ============================================================================
// 定位：上位机调参、下位机执行。本头文件只打包「要发送的控制帧」，不解析任何回包。
//       若需回读转速（流式接收 0xF0→速度回调），请改用 mdc_lite_ctrl.hpp 的 MdLite。
//
// 只涉及 3 条二进制命令：
//   0x31 MOTOR_CTRL     四通道控制目标（下位机执行）
//   0x40 SUBSCRIBE      开启状态周期上报（收速度的前提）
//   0x41 UNSUBSCRIBE    关闭状态上报（善后）
//
// 命名约定（LITE.md §1）：C/C++ 用 md_lite_ 前缀 / 命名空间 mdc_lite。本文件为 C++ 形态：
// 函数在命名空间 mdc_lite 内使用短名 ctrl/stop/subscribe/unsubscribe（与 Python 版 mdc_lite 同名同比）。
//
// 复用：本头文件直接复用同目录 mdc_lib.hpp（mdc::bin_motor_ctrl / bin_subscribe /
//       bin_unsubscribe），不改变协议字节布局（LE / CRC8 0x07 初值0 / [AA][CMD][LEN][DATA][CRC8]）。
//
// 串口由用户实现：本库只返回 std::vector<uint8_t>（完整帧，含 SYNC+CRC8），由用户 ser.write() 发送。
//
// 编译：g++ -std=c++17 -Wall -Wextra -o test_mdc_lite test_mdc_lite.cpp
// ============================================================================
#ifndef MDC_LITE_HPP
#define MDC_LITE_HPP

#include <cstdint>     // int32_t / uint16_t / uint8_t
#include <vector>      // std::vector

#include "mdc_lib.hpp" // mdc::bin_motor_ctrl / bin_subscribe / bin_unsubscribe

namespace mdc_lite {

// 目标值含义随各通道控制模式（协议规范 §3.3）：
//   open = PWM(±1000)  speed = RPM   pos = 0.1°(±3600=±360.0°)

// 0x31 MOTOR_CTRL —— 四通道控制目标（int32 LE），返回完整帧字节。
// 等价于 mdc::bin_motor_ctrl(m0,m1,m2,m3)。例：ctrl(100,-200,0,300) 的 DATA 段
// == 64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00。
inline std::vector<uint8_t> ctrl(int32_t m0, int32_t m1, int32_t m2, int32_t m3) {
    return mdc::bin_motor_ctrl(m0, m1, m2, m3);
}

// 便捷：四通道全零控制帧（急停/退出前发送，0x31 DATA=16B 全零）。
inline std::vector<uint8_t> stop() {
    return ctrl(0, 0, 0, 0);
}

// 0x40 SUBSCRIBE —— 开启状态周期上报，返回完整帧字节。
// interval_ms 建议 ≥20（固件钳位）。等价于 mdc::bin_subscribe(interval_ms)。
inline std::vector<uint8_t> subscribe(uint16_t interval_ms) {
    return mdc::bin_subscribe(interval_ms);
}

// 0x41 UNSUBSCRIBE —— 关闭状态上报，返回完整帧字节。
inline std::vector<uint8_t> unsubscribe() {
    return mdc::bin_unsubscribe();
}

} // namespace mdc_lite

#endif // MDC_LITE_HPP
