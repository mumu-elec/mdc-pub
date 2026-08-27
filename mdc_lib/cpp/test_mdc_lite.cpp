// ============================================================================
// test_mdc_lite.cpp — mdc_lite / mdc_lite_ctrl 一致性自测（无硬件，纯计算，自包含）
// ============================================================================
// 覆盖：LITE.md §2.2 全部验证向量（send-only 帧）+ §3 回调派发（流式 0xF0 解析）。
//       —— crc8 向量、ctrl(100,-200,0,300) DATA 段、subscribe(50) 帧、unsubscribe/stop；
//       —— 回调：垃圾/坏CRC/0x31 帧不触发；0xF0 帧(56B/72B)恰好触发一次且 rpm 正确。
// **自包含**：只 include 同族独立极简库 mdc_lite.hpp / mdc_lite_ctrl.hpp，
//       不 include mdc_lib.hpp，也不调用 mdc:: 任何函数。
//
// 编译：g++ -std=c++17 -Wall -Wextra -o test_mdc_lite test_mdc_lite.cpp
// 运行：./test_mdc_lite           （全 PASS 时 main 返回 0）
// ============================================================================
#include "mdc_lite.hpp"        // send-only：mdc_lite::ctrl/stop/subscribe/unsubscribe/crc8/build_frame
#include "mdc_lite_ctrl.hpp"    // 调用+回调：mdc_lite::MdLite

#include <array>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <initializer_list>
#include <vector>

using mdc_lite::MdLite;

// ── 简单断言框架 ─────────────────────────────────────────
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (cond) {                                                        \
            ++g_pass;                                                      \
        } else {                                                           \
            ++g_fail;                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                  \
    } while (0)

// ── 测试数据构造辅助（LE 写入） ──────────────────────────
static void put_u32(std::vector<uint8_t>& v, size_t off, uint32_t x) {
    v[off]     = static_cast<uint8_t>(x & 0xFF);
    v[off + 1] = static_cast<uint8_t>((x >> 8) & 0xFF);
    v[off + 2] = static_cast<uint8_t>((x >> 16) & 0xFF);
    v[off + 3] = static_cast<uint8_t>((x >> 24) & 0xFF);
}
static void put_i32(std::vector<uint8_t>& v, size_t off, int32_t x) {
    put_u32(v, off, static_cast<uint32_t>(x));
}
static void put_f32(std::vector<uint8_t>& v, size_t off, float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    put_u32(v, off, u);
}
static std::vector<uint8_t> make_bytes(std::initializer_list<uint8_t> l) {
    return std::vector<uint8_t>(l);
}

static void dump(const char* tag, const std::vector<uint8_t>& v) {
    std::printf("  %s[%zu]:", tag, v.size());
    for (uint8_t b : v) std::printf(" %02X", b);
    std::printf("\n");
}

// ============================================================================
// crc8 向量（LITE.md §6 一致性自检）
// ============================================================================
static void test_crc8_vectors() {
    CHECK(mdc_lite::crc8(make_bytes({0x01, 0x00})) == 0x15);
    const uint8_t s[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(mdc_lite::crc8(s, 9) == 0xF4);
}

// ============================================================================
// send-only：LITE.md §2.2 验证向量
// ============================================================================
static void test_send_only() {
    // ctrl(100,-200,0,300)：CMD=0x31, LEN=16, DATA=16B 向量
    const std::vector<uint8_t> frame = mdc_lite::ctrl(100, -200, 0, 300);
    const std::vector<uint8_t> data_expect = make_bytes({
        0x64, 0x00, 0x00, 0x00,   // 100  (LE int32)
        0x38, 0xFF, 0xFF, 0xFF,   // -200 (LE int32 补码)
        0x00, 0x00, 0x00, 0x00,   // 0
        0x2C, 0x01, 0x00, 0x00}); // 300  (LE int32)
    CHECK(frame.size() == 20);
    CHECK(frame[0] == 0xAA && frame[1] == 0x31 && frame[2] == 16);
    bool data_ok = true;
    for (size_t i = 0; i < data_expect.size(); ++i)
        if (frame[3 + i] != data_expect[i]) data_ok = false;
    CHECK(data_ok);
    if (!data_ok) dump("ctrl data", frame);
    CHECK(mdc_lite::crc8(frame.data() + 1, frame.size() - 2) == frame[frame.size() - 1]);

    // subscribe(50)：整帧 == AA 40 02 32 00 9E（CRC 对 40 02 32 00 计算，==0x9E）
    const std::vector<uint8_t> sub = mdc_lite::subscribe(50);
    CHECK(sub == make_bytes({0xAA, 0x40, 0x02, 0x32, 0x00, 0x9E}));
    CHECK(mdc_lite::crc8(sub.data() + 1, 4) == sub[5]);

    // stop == ctrl(0,0,0,0)；unsubscribe CMD=0x41
    CHECK(mdc_lite::stop() == mdc_lite::ctrl(0, 0, 0, 0));
    CHECK(mdc_lite::unsubscribe()[1] == 0x41);
    const std::vector<uint8_t> zf = mdc_lite::stop();
    CHECK(zf[0] == 0xAA && zf[1] == 0x31 && zf[2] == 16);
    for (size_t i = 3; i < 3 + 16; ++i) CHECK(zf[i] == 0x00);
}

// ============================================================================
// 0xF0 STATUS_REPORT 帧构造（56B / 72B）—— 用同族独立库 mdc_lite::build_frame
// ============================================================================
static std::vector<uint8_t> make_status56(const int32_t enc[4], const float tgt[4],
                                          const int32_t rpm[4],
                                          uint32_t frame_cnt, uint32_t ok_cnt) {
    std::vector<uint8_t> payload(56, 0);
    for (int i = 0; i < 4; ++i) {
        put_i32(payload, 0 + i * 4, enc[i]);      // enc @0
        put_f32(payload, 16 + i * 4, tgt[i]);     // tgt @16
        put_i32(payload, 32 + i * 4, rpm[i]);     // rpm @32
    }
    put_u32(payload, 48, frame_cnt);              // sbus_frame_cnt @48
    put_u32(payload, 52, ok_cnt);                 // sbus_ok_cnt @52
    return mdc_lite::build_frame(0xF0, payload);
}

static std::vector<uint8_t> make_status72(const int32_t enc[4], const float tgt[4],
                                          const int32_t rpm[4], const int32_t rpm_raw[4],
                                          uint32_t frame_cnt, uint32_t ok_cnt) {
    std::vector<uint8_t> payload(72, 0);
    for (int i = 0; i < 4; ++i) {
        put_i32(payload, 0 + i * 4, enc[i]);      // enc @0
        put_f32(payload, 16 + i * 4, tgt[i]);     // tgt @16
        put_i32(payload, 32 + i * 4, rpm[i]);     // rpm @32
        put_i32(payload, 48 + i * 4, rpm_raw[i]); // rpm_raw @48
    }
    put_u32(payload, 64, frame_cnt);              // sbus_frame_cnt @64
    put_u32(payload, 68, ok_cnt);                 // sbus_ok_cnt @68
    return mdc_lite::build_frame(0xF0, payload);
}

// ============================================================================
// 回调派发：仅 0xF0 触发，56B/72B 自动兼容，rpm 正确
// ============================================================================
static void test_ctrl_callback() {
    std::vector<std::array<int32_t, 4>> got;
    MdLite mdc([&got](const std::array<int32_t, 4>& rpm) { got.push_back(rpm); });

    // 1) 噪声 + 坏 CRC 帧 + 正常 0x31 控制帧 → 不应触发回调
    //    （先构造一帧 0x31 控制帧字节流，模拟上位机自己发的，解析器能识别但非 0xF0）
    std::vector<uint8_t> stream;
    const uint8_t junk[] = {0x00, 0x01, 0x02, 'h', 'i', 0x0A};
    const uint8_t bad[]  = {0xAA, 0x40, 0x02, 0x32, 0x00, 0x00};  // 坏 CRC（应为 0x9E）
    const std::vector<uint8_t> ctrlf = mdc_lite::ctrl(100, -200, 0, 300);
    stream.insert(stream.end(), junk, junk + sizeof(junk));
    stream.insert(stream.end(), bad, bad + sizeof(bad));
    stream.insert(stream.end(), ctrlf.begin(), ctrlf.end());
    for (uint8_t b : stream) mdc.feed(b);
    CHECK(got.empty());   // 非 0xF0 帧（含坏 CRC、控制帧）不触发回调

    // 2) 56B 状态帧整帧喂入 → 触发一次，rpm 四值正确
    const int32_t enc[4] = {100, 200, 300, 400};
    const float  tgt[4]  = {10.0f, 20.0f, 0.0f, 5.0f};
    const int32_t rpm[4] = {1234, -567, 0, 9000};
    const std::vector<uint8_t> f56 = make_status56(enc, tgt, rpm, 7, 6);
    for (uint8_t b : f56) mdc.feed(b);
    CHECK(got.size() == 1);
    if (got.size() == 1) {
        CHECK(got[0][0] == 1234 && got[0][1] == -567
              && got[0][2] == 0 && got[0][3] == 9000);
    }

    // 3) 72B 扩展帧也应识别（rpm_raw 偏移不同）
    got.clear();
    const int32_t raw4[4] = {11, 21, 31, 41};
    const int32_t rpm72[4] = {10, 20, 30, 40};
    const std::vector<uint8_t> f72 = make_status72(enc, tgt, rpm72, raw4, 1, 1);
    for (uint8_t b : f72) mdc.feed(b);
    CHECK(got.size() == 1);
    if (got.size() == 1) {
        CHECK(got[0][0] == 10 && got[0][1] == 20
              && got[0][2] == 30 && got[0][3] == 40);
    }

    // 4) reset 后重新同步可用
    got.clear();
    mdc.feed(0xAA); mdc.feed(0xF0);
    mdc.reset();
    for (uint8_t b : f56) mdc.feed(b);
    CHECK(got.size() == 1 && got[0][0] == 1234);

    // 5) 未注册回调时不崩溃
    MdLite silent;
    for (uint8_t b : f56) silent.feed(b);
    CHECK(true);   // 未派发，无异常即通过
}

// ============================================================================
// main
// ============================================================================
int main() {
    std::printf("=== mdc_lite 自测 ===\n");
    test_crc8_vectors();
    test_send_only();
    test_ctrl_callback();
    std::printf("PASS: %d, FAIL: %d\n", g_pass, g_fail);
    if (g_fail == 0) std::printf("ALL OK\n");
    return g_fail == 0 ? 0 : 1;
}
