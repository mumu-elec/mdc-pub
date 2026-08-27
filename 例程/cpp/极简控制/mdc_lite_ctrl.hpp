// ============================================================================
// mdc_lite_ctrl.hpp — 极简调用库（调用+回调接收，C++17 宿主版）
// ============================================================================
// 定位：上位机调参、下位机执行。在 mdc_lite（send-only）基础上，增加一个流式状态接收器：
//       你逐字节喂入收到的数据，遇到下位机主动推送的 0xF0 STATUS_REPORT 时自动解析
//       四通道转速（rpm[4]）并调用你注册的「速度回调」。
//
// 只涉及 4 条命令：0x31 MOTOR_CTRL、0x40/0x41 SUBSCRIBE/UNSUBSCRIBE、0xF0 STATUS_REPORT。
//
// 复用：本头文件 #include "mdc_lite.hpp"（发送侧）+ mdc_lib.hpp（mdc::Parser / mdc::parse_status）。
//       不改变协议字节布局（LE / CRC8 0x07 初值0 / [AA][CMD][LEN][DATA][CRC8]）。
//
// 串口由用户实现：发送侧返回 std::vector<uint8_t>（用户 ser.write()）；
//       接收侧由用户把收到的字节逐个调 feed() 喂入（0xF0 到达时自动回调 on_speed）。
//
// 编译：g++ -std=c++17 -Wall -Wextra -o test_mdc_lite test_mdc_lite.cpp
// ============================================================================
#ifndef MDC_LITE_CTRL_HPP
#define MDC_LITE_CTRL_HPP

#include <array>      // std::array
#include <cstdint>    // int32_t / uint16_t / uint8_t
#include <functional> // std::function
#include <utility>    // std::move
#include <vector>     // std::vector

#include "mdc_lite.hpp"      // 发送侧（ctrl/stop/subscribe/unsubscribe）
#include "mdc_lib.hpp"       // mdc::Parser / mdc::parse_status / mdc::Status / MD_CMD_STATUS_REPORT

namespace mdc_lite {

// ============================================================================
// class MdLite —— 极简控制器：发送侧（复用 mdc_lite）+ 速度回调接收侧
// ============================================================================
// 示例：
//     mdc_lite::MdLite mdc([](const std::array<int32_t,4>& rpm){
//         printf("rpm: %d %d %d %d\n", rpm[0], rpm[1], rpm[2], rpm[3]);
//     });
//     ser.write(mdc.subscribe(50));        // 先订阅（收速度的前提）
//     for (;;) {
//         ser.write(mdc.control(100, -200, 0, 300));   // 发送控制帧
//         for (uint8_t b : rx_bytes) mdc.feed(b);      // 0xF0 到达时自动回调
//     }
// ============================================================================
class MdLite {
public:
    // 速度回调签名（LITE.md §3.1）：四通道转速 rpm[4]。
    using OnSpeed = std::function<void(const std::array<int32_t, 4>&)>;

    // 构造：注册速度回调 on_speed(rpm)（rpm 为 4 元 int32 转速）；on_speed 可缺省（默认不派发）。
    explicit MdLite(OnSpeed on_speed = {}) : on_speed_(std::move(on_speed)) {}

    // 便捷：运行期改写/清空回调（可与构造传参等价）。
    void set_on_speed(OnSpeed cb) { on_speed_ = std::move(cb); }
    // 读取当前回调（可能为空）。
    const OnSpeed& on_speed() const { return on_speed_; }

    // ---- 发送侧（委托 mdc_lite，语义同 LITE.md §2）----
    // 0x31 MOTOR_CTRL 控制帧字节（同 mdc_lite::ctrl）。
    std::vector<uint8_t> control(int32_t m0, int32_t m1, int32_t m2, int32_t m3) const {
        return ctrl(m0, m1, m2, m3);
    }
    // 别名：与 mdc_lite::ctrl 同名的便捷写法（保持一致命名）。
    std::vector<uint8_t> ctrl(int32_t m0, int32_t m1, int32_t m2, int32_t m3) const {
        return mdc_lite::ctrl(m0, m1, m2, m3);
    }
    // 四通道全零控制帧字节（同 mdc_lite::stop）。
    std::vector<uint8_t> stop() const { return mdc_lite::stop(); }
    // 0x40 SUBSCRIBE 帧字节（同 mdc_lite::subscribe）。
    std::vector<uint8_t> subscribe(uint16_t interval_ms) const {
        return mdc_lite::subscribe(interval_ms);
    }
    // 0x41 UNSUBSCRIBE 帧字节（同 mdc_lite::unsubscribe）。
    std::vector<uint8_t> unsubscribe() const { return mdc_lite::unsubscribe(); }

    // ---- 接收侧（回调）----
    // 喂入一个收到的字节（0~255）。
    // 收到完整且 CRC 通过的 0xF0 STATUS_REPORT（56B/72B 自动兼容）时，解析四通道 rpm
    // 并调用注册的速度回调；其他帧（0x31 控制帧等）与噪声一律忽略。
    void feed(uint8_t byte) {
        auto r = parser_.feed(byte);           // mdc::Parser：滑动窗口 + CRC 校验
        if (!r) return;
        if (r->first != mdc::MD_CMD_STATUS_REPORT) return;   // 仅 0xF0 触发回调
        mdc::Status st;
        if (mdc::parse_status(r->second, st) && on_speed_)
            on_speed_(st.rpm);                 // (rpm0, rpm1, rpm2, rpm3)
    }

    // 清空流式解析器缓冲（切换连接 / 重新同步时调用）。
    void reset() { parser_.reset(); }

private:
    OnSpeed     on_speed_;
    mdc::Parser parser_;
};

} // namespace mdc_lite

#endif // MDC_LITE_CTRL_HPP
