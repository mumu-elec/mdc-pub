// ============================================================================
// mdc_lite_ctrl.hpp — 极简调用库（调用+回调接收，C++17 宿主版，独立实现）
// ============================================================================
// 定位：上位机调参、下位机执行。在 mdc_lite（send-only）基础上，增加一个流式状态接收器：
//       你逐字节喂入收到的数据，遇到下位机主动推送的 0xF0 STATUS_REPORT 时自动解析
//       四通道转速（rpm[4]）并调用你注册的「速度回调」。
//
// 只涉及 4 条命令：0x31 MOTOR_CTRL、0x40/0x41 SUBSCRIBE/UNSUBSCRIBE、0xF0 STATUS_REPORT。
//
// **独立实现**：发送侧复用同族独立极简库 mdc_lite.hpp（仅 #include 本同族头文件，不 include
//       mdc_lib.hpp）；接收侧自带流式解析器与 0xF0 STATUS_REPORT 解析（提取四通道 rpm），
//       不依赖 mdc:: 的任何函数。
//
// 串口由用户实现：发送侧返回 std::vector<uint8_t>（用户 ser.write()）；
//       接收侧由用户把收到的字节逐个调 feed() 喂入（0xF0 到达时自动回调 on_speed）。
//
// 编译：g++ -std=c++17 -Wall -Wextra -o test_mdc_lite test_mdc_lite.cpp
// ============================================================================
#ifndef MDC_LITE_CTRL_HPP
#define MDC_LITE_CTRL_HPP

#include <array>        // std::array
#include <cstddef>      // size_t
#include <cstdint>      // int32_t / uint16_t / uint8_t
#include <functional>   // std::function
#include <optional>     // std::optional
#include <utility>      // std::pair / std::make_pair / std::move
#include <vector>       // std::vector

#include "mdc_lite.hpp" // 同族独立极简库（发送侧：ctrl/stop/subscribe/unsubscribe/build_frame/crc8）

namespace mdc_lite {

inline constexpr uint8_t MD_CMD_STATUS_REPORT = 0xF0;   // MCU 主动推送状态上报

// ============================================================================
// 内部工具：小端序（LE）有符号 32 位读取（0xF0 帧内 rpm 字段 @32）
// ============================================================================
namespace detail {
inline int32_t rd_i32(const uint8_t* p) {
    const uint32_t u = static_cast<uint32_t>(p[0]) |
                       (static_cast<uint32_t>(p[1]) << 8) |
                       (static_cast<uint32_t>(p[2]) << 16) |
                       (static_cast<uint32_t>(p[3]) << 24);
    return static_cast<int32_t>(u);   // 位模式原样转有符号
}
} // namespace detail

// ============================================================================
// _StatusParser —— 流式解析器：逐字节喂入，自动找 0xAA 同步 + CRC8 校验；仅关注 0xF0。
// 与 mdc_lib 的 mdc::Parser 语义一致（滑动窗口 / 噪声丢弃 / 坏 CRC 重扫 / LEN 防护）。
// ============================================================================
class _StatusParser {
public:
    explicit _StatusParser(size_t max_data = MD_MAX_DATA) : max_data_(max_data) {}
    void reset() { buf_.clear(); }

    // 喂一个字节；收齐一帧且 CRC 通过 → 返回 {cmd, payload}，否则 std::nullopt
    std::optional<std::pair<uint8_t, std::vector<uint8_t>>> feed(uint8_t byte) {
        buf_.push_back(byte);
        return extract();
    }

private:
    // 滑动窗口：在缓冲中尝试提取一帧（可能连续提取，故用循环）
    std::optional<std::pair<uint8_t, std::vector<uint8_t>>> extract() {
        for (;;) {
            // 1) 找同步字 0xAA，之前的字节一律丢弃（文本回显等噪声）
            auto it = buf_.begin();
            while (it != buf_.end() && *it != MD_SYNC) ++it;
            if (it == buf_.end()) { buf_.clear(); return std::nullopt; }
            if (it != buf_.begin()) buf_.erase(buf_.begin(), it);
            // 2) 至少需要 CMD+LEN 才能继续
            if (buf_.size() < 3) return std::nullopt;
            const size_t ln = buf_[2];
            // 3) LEN 非法：丢弃该 0xAA，重扫
            if (ln > max_data_) { buf_.erase(buf_.begin()); continue; }
            const size_t fsize = 3 + ln + 1;             // CMD+LEN+DATA+CRC
            if (buf_.size() < fsize) return std::nullopt; // 未收全，继续等待
            // 4) CRC 校验
            if (crc8(buf_.data() + 1, fsize - 2) == buf_[fsize - 1]) {
                const uint8_t cmd = buf_[1];
                std::vector<uint8_t> payload(buf_.begin() + 3, buf_.begin() + fsize - 1);
                buf_.erase(buf_.begin(), buf_.begin() + fsize);
                return std::make_pair(cmd, std::move(payload));
            }
            // 5) CRC 失败：丢弃该 0xAA，继续向后找
            buf_.erase(buf_.begin());
        }
    }

    size_t max_data_;
    std::vector<uint8_t> buf_;
};

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
        auto r = parser_.feed(byte);
        if (!r) return;
        if (r->first != MD_CMD_STATUS_REPORT) return;   // 仅 0xF0 触发回调
        const std::vector<uint8_t>& payload = r->second;
        if (payload.size() != 56 && payload.size() != 72) return;   // 未知长度忽略
        std::array<int32_t, 4> rpm{};
        for (size_t i = 0; i < 4; ++i)
            rpm[i] = detail::rd_i32(payload.data() + 32 + i * 4);   // rpm @32
        if (on_speed_) on_speed_(rpm);
    }

    // 清空流式解析器缓冲（切换连接 / 重新同步时调用）。
    void reset() { parser_.reset(); }

private:
    OnSpeed        on_speed_;
    _StatusParser  parser_;
};

} // namespace mdc_lite

#endif // MDC_LITE_CTRL_HPP
