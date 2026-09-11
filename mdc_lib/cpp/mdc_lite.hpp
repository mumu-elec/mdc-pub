// ============================================================================
// mdc_lite.hpp — 极简调用库（只管调用 / send-only，C++17 宿主版，独立实现）
// ============================================================================
// 定位：上位机调参、下位机执行。本头文件只打包「要发送的控制帧」，不解析任何回包。
//       若需回读转速（流式接收 0xF0→速度回调），请改用 mdc_lite_ctrl.hpp 的 MdLite。
//
// **独立实现**：自带 CRC8、组帧与命令打包，**不依赖任何完整库**（不 include mdc_lib.hpp，
//       不调用 mdc:: 任何函数）。字节语义与 LITE.md §2 一致：
//       LE 小端 / CRC8 多项式 0x07 初值 0 / [0xAA][CMD][LEN][DATA][CRC8]，CRC 范围=CMD+LEN+DATA。
//
// 只涉及 3 条二进制命令：
//   0x31 MOTOR_CTRL     四通道控制目标（下位机执行）
//   0x40 SUBSCRIBE      开启状态周期上报（收速度的前提）
//   0x41 UNSUBSCRIBE    关闭状态上报（善后）
//
// 命名约定（LITE.md §1）：C/C++ 用 md_lite_ 前缀 / 命名空间 mdc_lite。本文件为 C++ 形态：
// 函数在命名空间 mdc_lite 内使用短名 ctrl/stop/subscribe/unsubscribe（与 Python 版 mdc_lite 同名同比）。
//
// 串口由用户实现：本库只返回 std::vector<uint8_t>（完整帧，含 SYNC+CRC8），由用户 ser.write() 发送。
//
// 编译：g++ -std=c++17 -Wall -Wextra -o test_mdc_lite test_mdc_lite.cpp
// ============================================================================
#ifndef MDC_LITE_HPP
#define MDC_LITE_HPP

#include <cstddef>     // size_t
#include <cstdint>     // int32_t / uint16_t / uint8_t
#include <vector>      // std::vector

namespace mdc_lite {

// ── 常量（帧格式约定）──
inline constexpr uint8_t  MD_SYNC      = 0xAA;   // 二进制帧同步字
inline constexpr uint16_t MD_MAX_DATA  = 248;    // DATA 段最大长度
inline constexpr uint8_t  MD_CRC8_POLY = 0x07;   // CRC8 多项式（初值 0）
inline constexpr uint8_t  MD_CMD_MOTOR_CTRL    = 0x31;   // 四通道控制
inline constexpr uint8_t  MD_CMD_SUBSCRIBE     = 0x40;   // 订阅状态上报
inline constexpr uint8_t  MD_CMD_UNSUBSCRIBE   = 0x41;   // 取消订阅

// ============================================================================
// crc8 —— 多项式 0x07，初值 0，按位计算（CRC-8/ATM）
// 校验向量：crc8([0x01,0x00])==0x15；crc8("123456789")==0xF4
// ============================================================================
inline uint8_t crc8(const uint8_t* data, size_t len) {
    uint8_t c = 0;
    for (size_t i = 0; i < len; ++i) {
        c ^= data[i];
        for (int b = 0; b < 8; ++b)
            c = (c & 0x80) ? static_cast<uint8_t>((c << 1) ^ MD_CRC8_POLY)
                           : static_cast<uint8_t>(c << 1);
    }
    return c;
}
inline uint8_t crc8(const std::vector<uint8_t>& data) {
    return data.empty() ? 0 : crc8(data.data(), data.size());
}

// ============================================================================
// build_frame —— 组帧 [0xAA][CMD][LEN][DATA...][CRC8]
// CRC 计算范围 = CMD+LEN+DATA（不含 SYNC）。DATA 由 LEN(1B) 编码，上限 248B。
// ============================================================================
inline std::vector<uint8_t> build_frame(uint8_t cmd, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> frame;
    frame.reserve(3 + data.size() + 1);
    frame.push_back(MD_SYNC);
    frame.push_back(cmd);
    frame.push_back(static_cast<uint8_t>(data.size()));
    frame.insert(frame.end(), data.begin(), data.end());
    frame.push_back(crc8(frame.data() + 1, frame.size() - 1));   // CMD+LEN+DATA
    return frame;
}
// 便捷：无 DATA 的空帧（UNSUBSCRIBE 等）
inline std::vector<uint8_t> build_frame(uint8_t cmd) {
    return build_frame(cmd, std::vector<uint8_t>{});
}

// ============================================================================
// 命令打包 —— 返回要发送的完整帧字节
// ============================================================================
// 0x31 MOTOR_CTRL —— 四通道控制目标（int32 LE），返回完整帧字节。
// 目标值含义随各通道控制模式：open=PWM(±1000) speed=RPM pos=0.1°(±3600=±360.0°)。
// 例：ctrl(100,-200,0,300) 的 DATA 段 == 64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00。
inline std::vector<uint8_t> ctrl(int32_t m0, int32_t m1, int32_t m2, int32_t m3) {
    std::vector<uint8_t> data;
    data.reserve(16);
    const uint32_t vals[4] = {
        static_cast<uint32_t>(m0), static_cast<uint32_t>(m1),
        static_cast<uint32_t>(m2), static_cast<uint32_t>(m3)};
    for (const uint32_t u : vals) {
        data.push_back(static_cast<uint8_t>(u & 0xFF));
        data.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
        data.push_back(static_cast<uint8_t>((u >> 16) & 0xFF));
        data.push_back(static_cast<uint8_t>((u >> 24) & 0xFF));
    }
    return build_frame(MD_CMD_MOTOR_CTRL, data);
}

// 便捷：四通道全零控制帧（急停/退出前发送，0x31 DATA=16B 全零）。
inline std::vector<uint8_t> stop() { return ctrl(0, 0, 0, 0); }

// 0x40 SUBSCRIBE —— 开启状态周期上报（interval_ms 建议 ≥20，固件钳位）。
inline std::vector<uint8_t> subscribe(uint16_t interval_ms) {
    std::vector<uint8_t> data;
    data.reserve(2);
    data.push_back(static_cast<uint8_t>(interval_ms & 0xFF));
    data.push_back(static_cast<uint8_t>((interval_ms >> 8) & 0xFF));
    return build_frame(MD_CMD_SUBSCRIBE, data);
}

// 0x41 UNSUBSCRIBE —— 关闭状态上报。
inline std::vector<uint8_t> unsubscribe() { return build_frame(MD_CMD_UNSUBSCRIBE); }

} // namespace mdc_lite

#endif // MDC_LITE_HPP
