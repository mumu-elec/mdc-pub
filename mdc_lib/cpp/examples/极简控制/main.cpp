// ============================================================================
// 例程：极简控制 —— 只用 mdc_lite 发送控制帧（只看发送，不解析回包）
// ============================================================================
// 定位：上位机调参、下位机执行。本程序只演示「要发送的帧怎么来」——全程只调用
// mdc_lite（send-only），把要写往串口的字节打印出来。串口收发由你实现：
//
//     ser.write(mdc_lite::ctrl(100, -200, 0, 300));   // 把打印出的帧字节发下去
//
// 只涉及 3 条命令：0x31 MOTOR_CTRL、0x40/0x41 SUBSCRIBE/UNSUBSCRIBE。
// 若需回读下位机 0xF0 状态并回调 rpm，请改用「控制+回调」例程（mdc_lite_ctrl::MdLite）。
//
// 编译：g++ -std=c++17 -Wall -Wextra -o lite_ctrl main.cpp
// 运行：./lite_ctrl           （在终端打印各帧字节，无需硬件/串口）
// ============================================================================

#include "mdc_lite.hpp"     // 极简库（send-only）：mdc_lite::ctrl/stop/subscribe/unsubscribe

#include <cstdio>
#include <vector>

// 打印一帧字节（HEX，用于展示库打包结果）
static void dump(const char* tag, const std::vector<uint8_t>& frame) {
    std::printf("%-16s [%2zu B] :", tag, frame.size());
    for (uint8_t b : frame) std::printf(" %02X", b);
    std::printf("\n");
}

int main() {
    std::printf("=== mdc_lite 极简控制（仅发送控制帧，串口由你实现） ===\n\n");

    // 1) 订阅状态上报（若需回读速度的前提；仅发送时可不订阅）
    dump("subscribe(50)", mdc_lite::subscribe(50));

    // 2) 四通道控制目标（int32 LE；含义随通道模式：open=PWM±1000 speed=RPM pos=0.1°）
    dump("ctrl(100,-200,0,300)", mdc_lite::ctrl(100, -200, 0, 300));

    // 3) 退出/急停前发送全零帧（0x31 DATA=16B 全零）
    dump("stop()", mdc_lite::stop());

    // 4) 取消订阅（善后）
    dump("unsubscribe()", mdc_lite::unsubscribe());

    std::printf("\n把上面每帧字节直接 ser.write() 即可发送给下位机执行。\n");
    std::printf("提示：subscribe(50) 整帧 == AA 40 02 32 00 9E（CRC=0x9E）。\n");
    return 0;
}
