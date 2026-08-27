// ============================================================================
// 例程：控制+回调 —— 用 mdc_lite_ctrl 发控制帧 + 收 0xF0 速度回调
// ============================================================================
// 定位：上位机调参、下位机执行。用 mdc_lite_ctrl::MdLite 同时获得「发送侧」（复用
// mdc_lite）与「回调接收侧」：你只需把收到的字节逐字节喂给 feed()，下位机主动推送的
// 0xF0 STATUS_REPORT 到达时，库自动解析四通道 rpm 并调用你注册的 on_speed 回调。
//
// 真实串口流程（串口由你实现）：
//     mdc_lite::MdLite mdc(on_speed);
//     ser.write(mdc.subscribe(50));                 // 先订阅（收速度的前提）
//     for (;;) {
//         ser.write(mdc.control(100, -200, 0, 300)); // 发送控制帧
//         for (uint8_t b : ser.read(64)) mdc.feed(b); // 0xF0 到达时自动回调 on_speed
//     }
//
// 本程序为无硬件演示：打印发送帧字节，并「模拟下位机上报」一帧 56B 的 0xF0 状态帧喂入，
// 展示回调被触发并打印四通道 rpm。串口收发部分已在注释中给出接入代码。
//
// 编译：g++ -std=c++17 -Wall -Wextra -o lite_ctrl_cb main.cpp
// 运行：./lite_ctrl_cb        （无需硬件/串口）
// ============================================================================

#include "mdc_lite_ctrl.hpp"   // 极简库（调用+回调）：mdc_lite::MdLite

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// 打印一帧字节（HEX，用于展示库打包结果）
static void dump(const char* tag, const std::vector<uint8_t>& frame) {
    std::printf("%-14s [%2zu B] :", tag, frame.size());
    for (uint8_t b : frame) std::printf(" %02X", b);
    std::printf("\n");
}

// 构造一帧 56B 的 0xF0 STATUS_REPORT（enc@0 + tgt@16 + rpm@32 + frame/ok cnt@48），
// 仅把四通道 rpm 填成指定值，其余字段为 0，用 mdc::build_frame 组帧（含 CRC）。
// 真实使用时该帧由下位机经串口主动推送，由库解析，无需自己构造。
static std::vector<uint8_t> make_status56(const int32_t rpm[4]) {
    std::vector<uint8_t> p(56, 0);
    for (int i = 0; i < 4; ++i) {                       // rpm @32，int32 LE
        const uint32_t u = static_cast<uint32_t>(rpm[i]);
        p[32 + i * 4 + 0] = static_cast<uint8_t>(u & 0xFF);
        p[32 + i * 4 + 1] = static_cast<uint8_t>((u >> 8) & 0xFF);
        p[32 + i * 4 + 2] = static_cast<uint8_t>((u >> 16) & 0xFF);
        p[32 + i * 4 + 3] = static_cast<uint8_t>((u >> 24) & 0xFF);
    }
    return mdc::build_frame(0xF0, p);
}

int main() {
    // 注册速度回调：on_speed(rpm) —— rpm 为 4 元 int32 转速数组
    mdc_lite::MdLite mdc([](const std::array<int32_t, 4>& rpm) {
        std::printf("  [速度回调] rpm = %d %d %d %d\n",
                    rpm[0], rpm[1], rpm[2], rpm[3]);
    });

    std::printf("=== mdc_lite_ctrl 控制+回调（串口由你实现，本程序为无硬件演示） ===\n\n");

    // ── 发送侧（委托 mdc_lite）──
    dump("subscribe(50)", mdc.subscribe(50));
    dump("control(100,-200,0,300)", mdc.control(100, -200, 0, 300));
    dump("stop()", mdc.stop());
    dump("unsubscribe()", mdc.unsubscribe());

    // ── 接收侧（回调）──
    std::printf("\n模拟下位机上报：构造一帧 0xF0(56B) 状态帧并逐字节喂入 feed()...\n");
    const int32_t rpm[4] = {1234, -567, 0, 9000};
    const std::vector<uint8_t> status = make_status56(rpm);
    dump("喂入 0xF0 帧", status);
    for (uint8_t b : status) mdc.feed(b);               // feed 触发 on_speed 回调

    std::printf("\n真实串口接入：\n");
    std::printf("  ser.write(mdc.subscribe(50));\n");
    std::printf("  for (;;) {\n");
    std::printf("      ser.write(mdc.control(100, -200, 0, 300));\n");
    std::printf("      for (uint8_t b : ser.read(64)) mdc.feed(b);  // 0xF0 自动回调\n");
    std::printf("  }\n");
    return 0;
}
