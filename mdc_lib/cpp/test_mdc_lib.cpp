// ============================================================================
// test_mdc_lib.cpp — mdc_lib 自测（无硬件，纯计算）
// 覆盖：API.md §8 全部验证向量 + config 往返 + 流式解析器
//       （垃圾字节 / 坏 CRC / 两帧连发 / LEN>250 防护 / 半帧）
// 编译：g++ -std=c++17 -Wall -Wextra -o test_mdc_lib test_mdc_lib.cpp
// 运行：./test_mdc_lib           （全 PASS 时 main 返回 0）
// ============================================================================
#include "mdc_lib.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace mdc;

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

// 期望抛出 std::invalid_argument
#define CHECK_THROW(expr)                                                  \
    do {                                                                   \
        bool thrown_ = false;                                              \
        try {                                                              \
            (void)(expr);                                                  \
        } catch (const std::invalid_argument&) {                           \
            thrown_ = true;                                                \
        } catch (...) {                                                    \
        }                                                                  \
        if (thrown_) {                                                     \
            ++g_pass;                                                      \
        } else {                                                           \
            ++g_fail;                                                      \
            std::printf("FAIL %s:%d: 期望抛出 invalid_argument: %s\n",     \
                        __FILE__, __LINE__, #expr);                        \
        }                                                                  \
    } while (0)

// ── 测试数据构造辅助（LE 写入） ──────────────────────────
static void put_u16(std::vector<uint8_t>& v, size_t off, uint16_t x) {
    v[off] = static_cast<uint8_t>(x & 0xFF);
    v[off + 1] = static_cast<uint8_t>((x >> 8) & 0xFF);
}
static void put_u32(std::vector<uint8_t>& v, size_t off, uint32_t x) {
    v[off] = static_cast<uint8_t>(x & 0xFF);
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

// ── 字节序列打印辅助（失败时输出） ──────────────────────
static void dump(const char* tag, const std::vector<uint8_t>& v) {
    std::printf("  %s[%zu]:", tag, v.size());
    for (uint8_t b : v) std::printf(" %02X", b);
    std::printf("\n");
}

// ============================================================================
// §8 一致性验证向量
// ============================================================================
static void test_crc8_vectors() {
    // crc8([0x01,0x00]) == 0x15
    const uint8_t a[] = {0x01, 0x00};
    CHECK(crc8(a, 2) == 0x15);
    // crc8("123456789") == 0xF4（CRC-8/ATM）
    const uint8_t b[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(crc8(b, 9) == 0xF4);
    CHECK(crc8(make_bytes({0x01, 0x00})) == 0x15);
}

static void test_build_frame_vectors() {
    // build_frame(0x01, b"") == AA 01 00 15
    const std::vector<uint8_t> ping = build_frame(0x01);
    CHECK(ping == make_bytes({0xAA, 0x01, 0x00, 0x15}));
    // build_frame(0x40, [0x32,0x00])（SUBSCRIBE 50ms）== AA 40 02 32 00 9E
    const std::vector<uint8_t> sub = build_frame(0x40, make_bytes({0x32, 0x00}));
    CHECK(sub == make_bytes({0xAA, 0x40, 0x02, 0x32, 0x00, 0x9E}));
    // 便捷重载：指针 + 长度
    const uint8_t d[] = {0x32, 0x00};
    CHECK(build_frame(0x40, d, 2) == sub);
    // DATA 超限抛异常
    CHECK_THROW(build_frame(0x01, std::vector<uint8_t>(251, 0)));
}

static void test_motor_ctrl_vector() {
    // motor_ctrl(100,-200,0,300) DATA == 64 00 00 00 38 FF FF FF 00 00 00 00 2C 01 00 00
    const std::vector<uint8_t> frame = bin_motor_ctrl(100, -200, 0, 300);
    const std::vector<uint8_t> expect_data =
        make_bytes({0x64, 0x00, 0x00, 0x00,
                    0x38, 0xFF, 0xFF, 0xFF,
                    0x00, 0x00, 0x00, 0x00,
                    0x2C, 0x01, 0x00, 0x00});
    // 帧头：AA 31 10
    CHECK(frame.size() == 20);
    CHECK(frame[0] == 0xAA && frame[1] == 0x31 && frame[2] == 0x10);
    // DATA 段逐字节比对
    bool data_ok = true;
    for (size_t i = 0; i < expect_data.size(); ++i)
        if (frame[3 + i] != expect_data[i]) data_ok = false;
    CHECK(data_ok);
    if (!data_ok) dump("motor_ctrl data", frame);
    // CRC 校验通过
    CHECK(crc8(frame.data() + 1, frame.size() - 2) == frame[frame.size() - 1]);
}

// ============================================================================
// 帧级解析
// ============================================================================
static void test_parse_frame() {
    // 有效帧
    Frame f = parse_frame(make_bytes({0xAA, 0x40, 0x02, 0x32, 0x00, 0x9E}));
    CHECK(f.valid);
    CHECK(f.cmd == 0x40);
    CHECK(f.payload == make_bytes({0x32, 0x00}));
    // 坏 CRC
    CHECK(!parse_frame(make_bytes({0xAA, 0x40, 0x02, 0x32, 0x00, 0x00})).valid);
    // 坏 SYNC
    CHECK(!parse_frame(make_bytes({0x00, 0x40, 0x02, 0x32, 0x00, 0x9E})).valid);
    // LEN 与实际长度不符
    CHECK(!parse_frame(make_bytes({0xAA, 0x40, 0x03, 0x32, 0x00, 0x9E})).valid);
    // 过短（<4）
    CHECK(!parse_frame(make_bytes({0xAA, 0x01, 0x00})).valid);
    // LEN 超上限
    CHECK(!parse_frame(make_bytes({0xAA, 0x01, 0xFF})).valid);
    // 空输入
    CHECK(!parse_frame(std::vector<uint8_t>{}).valid);
    // 空 PING 帧
    Frame p = parse_frame(make_bytes({0xAA, 0x01, 0x00, 0x15}));
    CHECK(p.valid && p.cmd == 0x01 && p.payload.empty());
}

// ============================================================================
// 流式解析器
// ============================================================================
static void test_parser_basic() {
    Parser p;
    // 半帧：AA 01 → 等待
    CHECK(!p.feed(0xAA).has_value());
    CHECK(!p.feed(0x01).has_value());
    CHECK(!p.feed(0x00).has_value());   // LEN=0，还差 CRC
    auto r = p.feed(0x15);              // 补上 CRC
    CHECK(r.has_value());
    if (r) {
        CHECK(r->first == 0x01);
        CHECK(r->second.empty());
    }
}

static void test_parser_garbage_badcrc_good() {
    // 垃圾字节 + 坏 CRC 帧 + 正常帧 → 只输出正常帧
    Parser p;
    std::vector<uint8_t> stream;
    const uint8_t junk[] = {0x00, 0x01, 0x02, 'h', 'i', 0x0A};   // 无 0xAA 的噪声
    const uint8_t bad[]  = {0xAA, 0x01, 0x00, 0x00};             // 坏 CRC（应为 0x15）
    const uint8_t good[] = {0xAA, 0x01, 0x00, 0x15};             // 正常 PING 帧
    stream.insert(stream.end(), junk, junk + sizeof(junk));
    stream.insert(stream.end(), bad, bad + sizeof(bad));
    stream.insert(stream.end(), good, good + sizeof(good));

    int frames = 0;
    for (uint8_t b : stream) {
        auto r = p.feed(b);
        if (r) {
            ++frames;
            CHECK(r->first == 0x01);
            CHECK(r->second.empty());
        }
    }
    CHECK(frames == 1);   // 垃圾与坏 CRC 均被丢弃
}

static void test_parser_two_frames() {
    // 两帧连发：PING + SUBSCRIBE(50ms)，依次输出
    Parser p;
    const std::vector<uint8_t> f1 = bin_ping();
    const std::vector<uint8_t> f2 = bin_subscribe(50);
    std::vector<uint8_t> got_cmd;
    std::vector<std::vector<uint8_t>> got_payload;
    for (uint8_t b : f1) { auto r = p.feed(b); if (r) { got_cmd.push_back(r->first); got_payload.push_back(r->second); } }
    for (uint8_t b : f2) { auto r = p.feed(b); if (r) { got_cmd.push_back(r->first); got_payload.push_back(r->second); } }
    CHECK(got_cmd.size() == 2);
    if (got_cmd.size() == 2) {
        CHECK(got_cmd[0] == 0x01);
        CHECK(got_cmd[1] == 0x40);
        CHECK(got_payload[0].empty());
        CHECK(got_payload[1] == make_bytes({0x32, 0x00}));
    }
}

static void test_parser_len_over_250() {
    // LEN>250 防护：非法 LEN 帧被丢弃重扫，后续正常帧仍能解析
    Parser p;
    const uint8_t bad[] = {0xAA, 0x01, 0xFF};   // LEN=255 > 250
    for (uint8_t b : bad) CHECK(!p.feed(b).has_value());
    const uint8_t good[] = {0xAA, 0x01, 0x00, 0x15};
    auto r = std::optional<std::pair<uint8_t, std::vector<uint8_t>>>{};
    for (uint8_t b : good) r = p.feed(b);
    CHECK(r.has_value());
    if (r) CHECK(r->first == 0x01);
}

static void test_parser_reset() {
    Parser p;
    p.feed(0xAA);
    p.feed(0x01);
    p.reset();                       // 清空缓冲
    auto r = p.feed(0x15);           // 只剩 CRC 字节，无法成帧
    CHECK(!r.has_value());
}

static void test_parser_batch() {
    // 批量 feed 便捷接口
    Parser p;
    const std::vector<uint8_t> f = bin_ping();
    auto frames = p.feed(f.data(), f.size());
    CHECK(frames.size() == 1);
    if (!frames.empty()) CHECK(frames[0].first == 0x01);
}

// ============================================================================
// 文本指令层
// ============================================================================
static void test_text() {
    CHECK(text_version() == "/version\n");
    CHECK(text_help()    == "/help\n");
    CHECK(text_status()  == "/status\n");
    CHECK(text_check()   == "/check\n");
    CHECK(text_detect()  == "/detect\n");
    CHECK(text_save()    == "/save\n");
    CHECK(text_load()    == "/load\n");
    CHECK(text_reset()   == "/reset\n");
    CHECK(text_enczero(1) == "/enczero 1\n");
    CHECK(text_enczero(4) == "/enczero 4\n");
    CHECK(text_mode(1)          == "/mode 1\n");          // 读取模式
    CHECK(text_mode(1, nullptr) == "/mode 1\n");
    CHECK(text_mode(1, "speed") == "/mode 1 speed\n");
    CHECK(text_mode(1, std::string("pos")) == "/mode 1 pos\n");
    // text_build 通用构造
    CHECK(text_build("/speedctrl", "1 0.5 0.02 0.01 500 800 10 0")
          == "/speedctrl 1 0.5 0.02 0.01 500 800 10 0\n");
    CHECK(text_build("/uart2", "115200 0 uart") == "/uart2 115200 0 uart\n");
    CHECK(text_build("/sbusrange", "172 1811")  == "/sbusrange 172 1811\n");
    CHECK(text_build("/mode") == "/mode\n");
    // 通道越界抛异常
    CHECK_THROW(text_enczero(0));
    CHECK_THROW(text_enczero(5));
    CHECK_THROW(text_mode(0));
    CHECK_THROW(text_mode(5, "open"));
}

// ============================================================================
// 二进制命令层
// ============================================================================
static void test_bin_basic() {
    CHECK(bin_ping()          == make_bytes({0xAA, 0x01, 0x00, 0x15}));
    CHECK(bin_read_param()[1] == 0x10 && bin_read_param()[2] == 0x00);
    CHECK(bin_save()[1]       == 0x20);
    CHECK(bin_load()[1]       == 0x21);
    CHECK(bin_factory_reset()[1] == 0x22);
    CHECK(bin_unsubscribe()[1]   == 0x41);
    CHECK(bin_enter_bl()[1]      == 0x52);
    CHECK(bin_reboot()[1]        == 0x53);
    CHECK(bin_subscribe(50) == make_bytes({0xAA, 0x40, 0x02, 0x32, 0x00, 0x9E}));
    // DEBUG_SBUS / DEBUG_SPEED
    CHECK(bin_debug_sbus(1)  == make_bytes({0xAA, 0x43, 0x01, 0x01, 0x29}));
    CHECK(bin_debug_speed(0) == make_bytes({0xAA, 0x44, 0x01, 0x00, 0x38}));
}

static void test_bin_motor_raw() {
    // ch=2, dir=0, pwm=350 → AA 30 04 02 00 5E 01 <crc>
    const std::vector<uint8_t> f = bin_motor_raw(2, 0, 350);
    CHECK(f[0] == 0xAA && f[1] == 0x30 && f[2] == 0x04);
    CHECK(f[3] == 0x02 && f[4] == 0x00 && f[5] == 0x5E && f[6] == 0x01);
    CHECK(crc8(f.data() + 1, f.size() - 2) == f[f.size() - 1]);
    // 参数校验
    CHECK_THROW(bin_motor_raw(4, 0, 100));
    CHECK_THROW(bin_motor_raw(0, 2, 100));
    CHECK_THROW(bin_motor_raw(0, 0, 1001));
}

static void test_bin_write_param() {
    // 231B 配置
    std::vector<uint8_t> cfg(231, 0x5A);
    const std::vector<uint8_t> f = bin_write_param(cfg);
    CHECK(f[0] == 0xAA && f[1] == 0x11 && f[2] == 231);
    CHECK(f.size() == 235);
    for (size_t i = 3; i < 3 + 231; ++i) CHECK(f[i] == 0x5A);
    CHECK(crc8(f.data() + 1, f.size() - 2) == f[f.size() - 1]);
    // 长度错误
    CHECK_THROW(bin_write_param(std::vector<uint8_t>(230, 0)));
    CHECK_THROW(bin_write_param(std::vector<uint8_t>(232, 0)));
}

static void test_bin_write_field() {
    // field_id=20(encoder_cpr[0]), value={0x2C,0x01} → DATA = 14 00 2C 01
    const std::vector<uint8_t> f = bin_write_field(20, make_bytes({0x2C, 0x01}));
    CHECK(f[0] == 0xAA && f[1] == 0x12 && f[2] == 0x04);
    CHECK(f[3] == 0x14 && f[4] == 0x00 && f[5] == 0x2C && f[6] == 0x01);
    CHECK(crc8(f.data() + 1, f.size() - 2) == f[f.size() - 1]);
    // 受保护区（offset<12）拒绝
    CHECK_THROW(bin_write_field(11, make_bytes({0x01})));
    CHECK_THROW(bin_write_field(0, make_bytes({0x01})));
    // offset=12 合法（baud_rate 低字节）
    CHECK(bin_write_field(12, make_bytes({0x00, 0x84, 0x1E, 0x00}))[1] == 0x12);
}

// ============================================================================
// 解析层：ACK / STATUS / DETECT / SBUS
// ============================================================================
static void test_parse_ack() {
    Ack a;
    CHECK(parse_ack(make_bytes({0x00}), a));
    CHECK(a.err == 0x00 && a.ok());
    CHECK(parse_ack(make_bytes({0xFF}), a));
    CHECK(a.err == 0xFF && !a.ok());
    CHECK(parse_ack(make_bytes({0x01}), a));   // 固件个别场景 0x01 → 视为失败
    CHECK(!a.ok());
    CHECK(parse_ack(make_bytes({0x02}), a));
    CHECK(!a.ok());
    // 长度错误
    CHECK(!parse_ack(std::vector<uint8_t>{}, a));
    CHECK(!parse_ack(make_bytes({0x00, 0x00}), a));
}

static void test_parse_status() {
    // ── 56B 常规模式 ──
    std::vector<uint8_t> p56(56, 0);
    const int32_t enc[4] = {1, -2, 3, -4};
    const float  tgt[4]  = {100.5f, -200.25f, 0.5f, 999.0f};
    const int32_t rpm[4] = {10, -20, 30, -40};
    for (int i = 0; i < 4; ++i) {
        put_i32(p56, 0 + i * 4, enc[i]);
        put_f32(p56, 16 + i * 4, tgt[i]);
        put_i32(p56, 32 + i * 4, rpm[i]);
    }
    put_u32(p56, 48, 0x11223344);   // sbus_frame_cnt
    put_u32(p56, 52, 0x55667788);   // sbus_ok_cnt

    Status s;
    CHECK(parse_status(p56, s));
    CHECK(s.extended == 0);
    bool ok56 = true;
    for (int i = 0; i < 4; ++i) {
        if (s.enc[i] != enc[i] || s.rpm[i] != rpm[i] || s.tgt[i] != tgt[i]) ok56 = false;
        if (s.rpm_raw[i] != 0) ok56 = false;      // 56B 模式 rpm_raw 恒 0
    }
    CHECK(ok56);
    CHECK(s.sbus_frame_cnt == 0x11223344);
    CHECK(s.sbus_ok_cnt == 0x55667788);

    // ── 72B 扩展模式 ──
    std::vector<uint8_t> p72(72, 0);
    const int32_t raw[4] = {11, -22, 33, -44};
    for (int i = 0; i < 4; ++i) {
        put_i32(p72, 0 + i * 4, enc[i]);
        put_f32(p72, 16 + i * 4, tgt[i]);
        put_i32(p72, 32 + i * 4, rpm[i]);
        put_i32(p72, 48 + i * 4, raw[i]);         // rpm_raw @48
    }
    put_u32(p72, 64, 0xAABBCCDD);   // sbus_frame_cnt @64
    put_u32(p72, 68, 0x01020304);   // sbus_ok_cnt @68

    CHECK(parse_status(p72, s));
    CHECK(s.extended == 1);
    bool ok72 = true;
    for (int i = 0; i < 4; ++i) {
        if (s.enc[i] != enc[i] || s.rpm[i] != rpm[i] || s.tgt[i] != tgt[i]) ok72 = false;
        if (s.rpm_raw[i] != raw[i]) ok72 = false;
    }
    CHECK(ok72);
    CHECK(s.sbus_frame_cnt == 0xAABBCCDD);
    CHECK(s.sbus_ok_cnt == 0x01020304);

    // ── 非法长度 → false ──
    CHECK(!parse_status(make_bytes({0x01, 0x02, 0x03}), s));
    CHECK(!parse_status(std::vector<uint8_t>(57, 0), s));
    CHECK(!parse_status(std::vector<uint8_t>{}, s));
}

static void test_parse_detect() {
    Detect d;
    // proto=2(UART) inv=1 baud=115200(0x0001C200)
    CHECK(parse_detect(make_bytes({0x02, 0x01, 0x00, 0xC2, 0x01, 0x00}), d));
    CHECK(d.proto == 2 && d.inv == 1 && d.baud == 115200);
    // proto=0 识别失败
    CHECK(parse_detect(make_bytes({0x00, 0x00, 0x00, 0x00, 0x00, 0x00}), d));
    CHECK(d.proto == 0 && d.baud == 0);
    // 长度错误
    CHECK(!parse_detect(make_bytes({0x02, 0x01, 0x00}), d));
    CHECK(!parse_detect(std::vector<uint8_t>{}, d));
}

static void test_parse_sbus() {
    std::vector<uint8_t> p(32, 0);
    for (int i = 0; i < 16; ++i) put_u16(p, i * 2, static_cast<uint16_t>(i * 100 + 1));
    std::array<uint16_t, 16> ch{};
    CHECK(parse_sbus(p, ch));
    bool ok = true;
    for (int i = 0; i < 16; ++i)
        if (ch[static_cast<size_t>(i)] != static_cast<uint16_t>(i * 100 + 1)) ok = false;
    CHECK(ok);
    // 长度错误
    CHECK(!parse_sbus(make_bytes({0x01}), ch));
    CHECK(!parse_sbus(std::vector<uint8_t>{}, ch));
}

// ============================================================================
// config_t：parse_config ↔ pack_config 往返无损 + 位域 + 受保护区
// ============================================================================
static Config make_test_config() {
    Config c;
    c.baud_rate      = 2000000;
    c.cmd_timeout_ms = 500;
    c.protocol       = 2;      // UART
    c.sbus_inv       = 1;
    c.ctrl_priority  = 1;      // USB 优先
    for (int i = 0; i < 4; ++i) {
        const size_t ci = static_cast<size_t>(i);
        c.control_mode[ci]    = static_cast<uint8_t>(i % 3);          // 0,1,2,0
        c.motor_invert[ci]    = static_cast<uint8_t>(i);              // 0,1,2,3（含 bit1 编码器极性）
        c.encoder_cpr[ci]     = static_cast<uint16_t>(11 + i * 11);
        c.speed_period_ms[ci] = static_cast<uint16_t>(5 + i * 3);
        c.speed_pid_type[ci]  = static_cast<uint8_t>(i % 2);
        c.speed_olim[ci]      = static_cast<uint16_t>(1000 - i * 100);
        c.speed_kp[ci]   = 1.0f + i * 0.5f;
        c.speed_ki[ci]   = 0.1f * (i + 1);
        c.speed_kd[ci]   = 0.01f * (i + 1);
        c.speed_ilim[ci] = 500.0f - i * 100.0f;
        c.pos_period_ms[ci] = static_cast<uint16_t>(10 + i * 5);
        c.pos_pid_type[ci]  = static_cast<uint8_t>((i + 1) % 2);
        c.pos_kp[ci]   = 2.0f + i * 1.25f;
        c.pos_ki[ci]   = 0.05f * (i + 1);
        c.pos_kd[ci]   = 0.5f + i * 0.25f;
        c.pos_ilim[ci] = 3600.0f - i * 100.0f;
        c.pos_olim[ci] = 800.0f + i * 50.0f;
        c.pos_angle_cpr[ci] = static_cast<uint16_t>(i * 100);
        c.speed_filter_type[ci]   = static_cast<uint8_t>(i);          // 0,1,2,3
        c.speed_filter_window[ci] = static_cast<uint8_t>(1 + i * 15); // 1,16,31,46
        c.sbus_channel[ci] = static_cast<uint8_t>(i * 5 + 1);         // 1,6,11,16
        c.rc_dir_ch[ci]    = static_cast<uint8_t>(16 - i * 5);        // 16,11,6,1
        c.rc_map_mode[ci]  = static_cast<uint8_t>(i % 2);
        c.rc_dir_en[ci]    = static_cast<uint8_t>((i + 1) % 2);
        c.sbus_param[ci]   = static_cast<uint16_t>(1000 + i * 1000);
    }
    c.sbus_range_min = 172;
    c.sbus_range_max = 1811;
    return c;
}

static bool config_equal(const Config& a, const Config& b) {
    if (a.baud_rate != b.baud_rate || a.cmd_timeout_ms != b.cmd_timeout_ms) return false;
    if (a.protocol != b.protocol || a.sbus_inv != b.sbus_inv ||
        a.ctrl_priority != b.ctrl_priority) return false;
    for (int i = 0; i < 4; ++i) {
        const size_t ci = static_cast<size_t>(i);
        if (a.control_mode[ci] != b.control_mode[ci]) return false;
        if (a.motor_invert[ci] != b.motor_invert[ci]) return false;
        if (a.encoder_cpr[ci] != b.encoder_cpr[ci]) return false;
        if (a.speed_period_ms[ci] != b.speed_period_ms[ci]) return false;
        if (a.speed_pid_type[ci] != b.speed_pid_type[ci]) return false;
        if (a.speed_olim[ci] != b.speed_olim[ci]) return false;
        if (a.speed_kp[ci] != b.speed_kp[ci]) return false;
        if (a.speed_ki[ci] != b.speed_ki[ci]) return false;
        if (a.speed_kd[ci] != b.speed_kd[ci]) return false;
        if (a.speed_ilim[ci] != b.speed_ilim[ci]) return false;
        if (a.pos_period_ms[ci] != b.pos_period_ms[ci]) return false;
        if (a.pos_pid_type[ci] != b.pos_pid_type[ci]) return false;
        if (a.pos_kp[ci] != b.pos_kp[ci]) return false;
        if (a.pos_ki[ci] != b.pos_ki[ci]) return false;
        if (a.pos_kd[ci] != b.pos_kd[ci]) return false;
        if (a.pos_ilim[ci] != b.pos_ilim[ci]) return false;
        if (a.pos_olim[ci] != b.pos_olim[ci]) return false;
        if (a.pos_angle_cpr[ci] != b.pos_angle_cpr[ci]) return false;
        if (a.speed_filter_type[ci] != b.speed_filter_type[ci]) return false;
        if (a.speed_filter_window[ci] != b.speed_filter_window[ci]) return false;
        if (a.sbus_channel[ci] != b.sbus_channel[ci]) return false;
        if (a.rc_dir_ch[ci] != b.rc_dir_ch[ci]) return false;
        if (a.rc_map_mode[ci] != b.rc_map_mode[ci]) return false;
        if (a.rc_dir_en[ci] != b.rc_dir_en[ci]) return false;
        if (a.sbus_param[ci] != b.sbus_param[ci]) return false;
    }
    if (a.sbus_range_min != b.sbus_range_min) return false;
    if (a.sbus_range_max != b.sbus_range_max) return false;
    return true;
}

static void test_config_roundtrip() {
    const Config src = make_test_config();

    // 1) pack → parse 往返无损（位域、float、数组全部一致）
    const std::vector<uint8_t> raw = pack_config(src);
    CHECK(raw.size() == 231);
    Config back;
    CHECK(parse_config(raw, back));
    CHECK(config_equal(src, back));
    if (!config_equal(src, back))
        std::printf("  config round-trip 不一致\n");

    // 2) 位级往返：pack 后再 pack 应逐字节相同（确定性）
    const std::vector<uint8_t> raw2 = pack_config(back);
    CHECK(raw == raw2);

    // 3) 受保护区（offset 0~10）打包后必须全 0
    bool protected_zero = true;
    for (size_t i = 0; i < 11; ++i)
        if (raw[i] != 0) protected_zero = false;
    CHECK(protected_zero);
    if (!protected_zero) dump("pack_config 受保护区", raw);

    // 4) parse 忽略受保护区内容：手工填非零后解析结果不变
    std::vector<uint8_t> raw_hdr = raw;
    for (size_t i = 0; i < 11; ++i) raw_hdr[i] = static_cast<uint8_t>(0x80 + i);
    Config back2;
    CHECK(parse_config(raw_hdr, back2));
    CHECK(config_equal(back2, src));

    // 5) bin_write_param(Config) 便捷重载与 231B 字节版一致
    CHECK(bin_write_param(src) == bin_write_param(raw));

    // 6) 长度错误
    CHECK(!parse_config(std::vector<uint8_t>(230, 0), back));
    CHECK(!parse_config(std::vector<uint8_t>{}, back));
}

// ============================================================================
// main
// ============================================================================
int main() {
    std::printf("=== mdc_lib 自测 ===\n");
    test_crc8_vectors();
    test_build_frame_vectors();
    test_motor_ctrl_vector();
    test_parse_frame();
    test_parser_basic();
    test_parser_garbage_badcrc_good();
    test_parser_two_frames();
    test_parser_len_over_250();
    test_parser_reset();
    test_parser_batch();
    test_text();
    test_bin_basic();
    test_bin_motor_raw();
    test_bin_write_param();
    test_bin_write_field();
    test_parse_ack();
    test_parse_status();
    test_parse_detect();
    test_parse_sbus();
    test_config_roundtrip();
    std::printf("PASS: %d, FAIL: %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
