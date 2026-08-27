// ============================================================================
// mdc_lib.hpp — Motor Driver Controller 通用调用库（C++17 宿主版）
// ============================================================================
// 定位：只负责「打包要发送的数据」与「解析收到的数据」，串口收发由用户自己实现。
// 协议依据：mdc_lib/API.md（统一 API 规范，权威）+ 协议规范.md（布局 v2.1，config_t=231B）
// 特性：header-only、命名空间 mdc、零第三方依赖（仅标准库）、UTF-8 中文注释。
//
// 返回形态约定（与 API.md §2.2/§2.3 语义对齐，本实现选「返回容器」风格）：
//   - 打包函数返回 std::vector<uint8_t>（文本函数返回 std::string，含结尾 '\n'）；
//     参数非法（长度超限 / 通道越界 / 值越界等）抛 std::invalid_argument。
//   - 解析函数返回 bool，结果写入输出参数（结构体引用）；
//     payload 长度不符 / CRC 失败等返回 false，输出参数保持不变（不抛异常）。
//   - 流式解析器 Parser::feed(uint8_t) 返回
//     std::optional<std::pair<uint8_t, std::vector<uint8_t>>>，完整且 CRC 通过的一帧
//     返回 {cmd, payload}，否则返回 std::nullopt。
// ============================================================================
#ifndef MDC_LIB_HPP
#define MDC_LIB_HPP

#include <algorithm>   // std::find
#include <array>       // std::array
#include <cstdint>     // uint8_t / uint16_t / uint32_t / int32_t
#include <cstring>     // std::memcpy
#include <optional>    // std::optional
#include <stdexcept>   // std::invalid_argument
#include <string>      // std::string / std::to_string
#include <utility>     // std::pair / std::move / std::make_pair
#include <vector>      // std::vector

namespace mdc {

// ─────────────────────────── 常量（API.md §2.5） ───────────────────────────
inline constexpr uint8_t  MD_SYNC        = 0xAA;   // 二进制帧同步字
inline constexpr uint16_t MD_MAX_DATA    = 250;    // DATA 段最大长度
inline constexpr uint16_t MD_CONFIG_SIZE = 231;    // config_t 大小
inline constexpr uint16_t MD_FRAME_MAX   = 235;    // 最大整帧长度（4 + 231）
inline constexpr uint8_t  MD_CRC8_POLY   = 0x07;   // CRC8 多项式
inline constexpr uint8_t  MD_CMD_PING    = 0x01;   // 二进制命令号（完整表见下）
inline constexpr uint8_t  MD_ERR_OK      = 0x00;   // ACK 成功
inline constexpr uint8_t  MD_ERR_FAIL    = 0xFF;   // ACK 失败
inline constexpr size_t   MD_PARSER_BUF  = 256;    // 流式解析器缓冲上限（可裁剪，对齐 API.md §3.4）

// ── 二进制命令号（协议规范 §3.3，18 条；PING=0x01 见上方 §2.5 常量） ──
inline constexpr uint8_t MD_CMD_READ_PARAM    = 0x10;
inline constexpr uint8_t MD_CMD_WRITE_PARAM   = 0x11;
inline constexpr uint8_t MD_CMD_WRITE_FIELD   = 0x12;
inline constexpr uint8_t MD_CMD_SAVE_EEPROM   = 0x20;
inline constexpr uint8_t MD_CMD_LOAD_EEPROM   = 0x21;
inline constexpr uint8_t MD_CMD_FACTORY_RESET = 0x22;
inline constexpr uint8_t MD_CMD_MOTOR_RAW     = 0x30;
inline constexpr uint8_t MD_CMD_MOTOR_CTRL    = 0x31;
inline constexpr uint8_t MD_CMD_SUBSCRIBE     = 0x40;
inline constexpr uint8_t MD_CMD_UNSUBSCRIBE   = 0x41;
inline constexpr uint8_t MD_CMD_DEBUG_SBUS    = 0x43;
inline constexpr uint8_t MD_CMD_DEBUG_SPEED   = 0x44;
inline constexpr uint8_t MD_CMD_ENTER_BL      = 0x52;
inline constexpr uint8_t MD_CMD_REBOOT        = 0x53;
inline constexpr uint8_t MD_CMD_STATUS_REPORT = 0xF0;  // MCU 主动推送
inline constexpr uint8_t MD_CMD_DETECT_REPORT = 0xF1;  // MCU 主动推送
inline constexpr uint8_t MD_CMD_SBUS_DATA     = 0xF2;  // MCU 主动推送

// 小内存平台可用 MD_ENABLE_CONFIG=0 裁掉 config 全字段函数（API.md §6.5）
#ifndef MD_ENABLE_CONFIG
#define MD_ENABLE_CONFIG 1
#endif

// ============================================================================
// 内部工具：小端序（LE）多字节读写，一律手动移位拼装（不用 union/结构体对齐）
// ============================================================================
namespace detail {

// ── 读取（LE） ──
inline uint16_t rd_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8);
}
inline uint32_t rd_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
inline int32_t rd_i32(const uint8_t* p) {
    return static_cast<int32_t>(rd_u32(p));   // 位模式原样转有符号
}
inline float rd_f32(const uint8_t* p) {
    const uint32_t u = rd_u32(p);
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(f));           // 位级转换，保证往返无损
    return f;
}

// ── 写入（LE，vector 内指定偏移） ──
inline void wr_u16(std::vector<uint8_t>& v, size_t off, uint16_t val) {
    v[off]     = static_cast<uint8_t>(val & 0xFF);
    v[off + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
}
inline void wr_u32(std::vector<uint8_t>& v, size_t off, uint32_t val) {
    v[off]     = static_cast<uint8_t>(val & 0xFF);
    v[off + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
    v[off + 2] = static_cast<uint8_t>((val >> 16) & 0xFF);
    v[off + 3] = static_cast<uint8_t>((val >> 24) & 0xFF);
}
inline void wr_f32(std::vector<uint8_t>& v, size_t off, float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    wr_u32(v, off, u);
}

// ── 写入（LE，尾部追加，用于组 DATA 段） ──
inline void wr_u16_append(std::vector<uint8_t>& v, uint16_t val) {
    v.push_back(static_cast<uint8_t>(val & 0xFF));
    v.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
}
inline void wr_u32_append(std::vector<uint8_t>& v, uint32_t val) {
    v.push_back(static_cast<uint8_t>(val & 0xFF));
    v.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}
inline void wr_i32_append(std::vector<uint8_t>& v, int32_t val) {
    wr_u32_append(v, static_cast<uint32_t>(val));   // 补码位模式直接写入
}

} // namespace detail

// ============================================================================
// §3.1 CRC8 —— 多项式 0x07，初值 0，按位计算（协议规范 §3.1 逐行移植）
// 校验向量：crc8([0x01,0x00])==0x15；crc8("123456789")==0xF4（CRC-8/ATM）
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
// §3.2 组帧 —— [0xAA][CMD][LEN][DATA...][CRC8]
// CRC 计算范围 = CMD+LEN+DATA（不含 SYNC）；DATA 超 250B 抛 std::invalid_argument
// ============================================================================
inline std::vector<uint8_t> build_frame(uint8_t cmd, const std::vector<uint8_t>& data) {
    if (data.size() > MD_MAX_DATA)
        throw std::invalid_argument("mdc::build_frame: DATA 长度超过上限 250B");
    std::vector<uint8_t> frame;
    frame.reserve(3 + data.size() + 1);
    frame.push_back(MD_SYNC);
    frame.push_back(cmd);
    frame.push_back(static_cast<uint8_t>(data.size()));
    frame.insert(frame.end(), data.begin(), data.end());
    frame.push_back(crc8(frame.data() + 1, frame.size() - 1));  // CMD+LEN+DATA
    return frame;
}
// 便捷：无 DATA 的空帧（PING / SAVE 等）
inline std::vector<uint8_t> build_frame(uint8_t cmd) {
    return build_frame(cmd, std::vector<uint8_t>{});
}
// 便捷：C 风格指针 + 长度
inline std::vector<uint8_t> build_frame(uint8_t cmd, const uint8_t* data, size_t len) {
    return build_frame(cmd, std::vector<uint8_t>(data, data + len));
}

// ============================================================================
// §3.3 帧级解析 —— 校验 SYNC 与 CRC
// ============================================================================
struct Frame {
    uint8_t  cmd = 0;                // 命令号（valid 时有效）
    std::vector<uint8_t> payload;    // DATA 段拷贝（本实现选安全拷贝，非零拷贝）
    bool     valid = false;          // SYNC 与 CRC 均通过
};

inline Frame parse_frame(const uint8_t* frame, size_t len) {
    Frame f;
    if (frame == nullptr || len < 4) return f;          // 至少 CMD+LEN+CRC
    if (frame[0] != MD_SYNC) return f;                  // SYNC 不符
    const size_t ln = frame[2];
    if (ln > MD_MAX_DATA) return f;                     // LEN 非法
    if (ln + 4 != len) return f;                        // 声明长度与实际不符
    if (crc8(frame + 1, len - 2) != frame[len - 1]) return f;  // CRC 失败
    f.cmd = frame[1];
    f.payload.assign(frame + 3, frame + 3 + ln);
    f.valid = true;
    return f;
}
inline Frame parse_frame(const std::vector<uint8_t>& frame) {
    return parse_frame(frame.data(), frame.size());
}

// ============================================================================
// §3.4 流式解析器 —— 逐字节喂入，滑动窗口找 0xAA + CRC 校验 + LEN>250 防护
// 文本回显等噪声字节会在 0xAA 之前被丢弃；坏 CRC 帧丢弃该 0xAA 后继续重扫。
// ============================================================================
class Parser {
public:
    // max_data：DATA 段长度上限（默认 250；小内存平台可调小）
    explicit Parser(size_t max_data = MD_MAX_DATA) : max_data_(max_data) {}

    // 清空内部缓冲（切换连接 / 重新同步时调用）
    void reset() { buf_.clear(); }

    // 喂一个字节；收齐一帧且 CRC 通过 → 返回 {cmd, payload}，否则 std::nullopt
    std::optional<std::pair<uint8_t, std::vector<uint8_t>>> feed(uint8_t byte) {
        buf_.push_back(byte);
        return extract();
    }

    // 批量喂入（等价于逐字节 feed；多个完整帧会依次返回）
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> feed(
        const uint8_t* data, size_t len) {
        std::vector<std::pair<uint8_t, std::vector<uint8_t>>> out;
        for (size_t i = 0; i < len; ++i) {
            auto r = feed(data[i]);
            if (r) out.push_back(std::move(*r));
        }
        return out;
    }

private:
    // 滑动窗口：在缓冲中尝试提取一帧（可能连续提取，故用循环）
    std::optional<std::pair<uint8_t, std::vector<uint8_t>>> extract() {
        for (;;) {
            // 1) 找同步字 0xAA，之前的字节一律丢弃（文本回显等噪声）
            auto it = std::find(buf_.begin(), buf_.end(), MD_SYNC);
            if (it == buf_.end()) { buf_.clear(); return std::nullopt; }
            if (it != buf_.begin()) buf_.erase(buf_.begin(), it);
            // 2) 至少需要 CMD+LEN 才能继续
            if (buf_.size() < 3) return std::nullopt;
            const size_t ln = buf_[2];
            // 3) LEN>250 非法：丢弃该 0xAA，重扫
            if (ln > max_data_) { buf_.erase(buf_.begin()); continue; }
            const size_t fsize = 3 + ln + 1;             // CMD+LEN+DATA+CRC
            if (buf_.size() < fsize) return std::nullopt; // 未收全，继续等待
            // 4) CRC 校验
            if (crc8(buf_.data() + 1, fsize - 2) == buf_[fsize - 1]) {
                const uint8_t cmd = buf_[1];
                std::vector<uint8_t> payload(buf_.begin() + 3,
                                             buf_.begin() + fsize - 1);
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
// §4 文本指令层 —— 返回含结尾 '\n' 的 std::string（UTF-8）
// ============================================================================

// §4.1 通用构造：cmd="/mode"，args="1 speed" → "/mode 1 speed\n"
//      args 缺省（nullptr）→ "/mode\n"（省略参数 = 读取模式）
inline std::string text_build(const std::string& cmd) {
    return cmd + "\n";
}
inline std::string text_build(const std::string& cmd, const std::string& args) {
    return cmd + " " + args + "\n";
}
inline std::string text_build(const std::string& cmd, const std::string* args) {
    return (args == nullptr) ? text_build(cmd) : text_build(cmd, *args);
}

// §4.2 便捷封装（8 个无参 + enczero/mode 2 个带参）
inline std::string text_version() { return text_build("/version"); }
inline std::string text_help()    { return text_build("/help"); }
inline std::string text_status()  { return text_build("/status"); }
inline std::string text_check()   { return text_build("/check"); }
inline std::string text_detect()  { return text_build("/detect"); }
inline std::string text_save()    { return text_build("/save"); }
inline std::string text_load()    { return text_build("/load"); }
inline std::string text_reset()   { return text_build("/reset"); }

// /enczero <ch>：清零指定通道编码器（ch=1~4，越界抛 std::invalid_argument）
inline std::string text_enczero(uint8_t ch) {
    if (ch < 1 || ch > 4)
        throw std::invalid_argument("mdc::text_enczero: ch 取值 1~4");
    return text_build("/enczero", std::to_string(ch));
}

// /mode <ch> [open|speed|pos]：mode==nullptr → 读取模式 "/mode <ch>\n"
inline std::string text_mode(uint8_t ch, const std::string* mode = nullptr) {
    if (ch < 1 || ch > 4)
        throw std::invalid_argument("mdc::text_mode: ch 取值 1~4");
    if (mode == nullptr) return text_build("/mode", std::to_string(ch));
    return text_build("/mode", std::to_string(ch) + " " + *mode);
}
inline std::string text_mode(uint8_t ch, const std::string& mode) {
    return text_mode(ch, &mode);
}

// 其余指令（speedctrl/posctrl/cpr/inv/einv/posangle/filter/uart2/priority/
// timeout/smap/rmap/dmap/sbusparam/sbusrange）统一用 text_build 构造，例如：
//   text_build("/speedctrl", "1 0.5 0.02 0.01 500 800 10 0")
//   text_build("/uart2",     "115200 0 uart")
//   text_build("/sbusrange", "172 1811")

// ============================================================================
// §5 二进制命令层（15 个打包函数，返回整帧字节）
// ============================================================================

// 0x01 PING —— 连通性测试（无 DATA）
inline std::vector<uint8_t> bin_ping() { return build_frame(MD_CMD_PING); }

// 0x10 READ_PARAM —— 读取全部配置（无 DATA）
inline std::vector<uint8_t> bin_read_param() { return build_frame(MD_CMD_READ_PARAM); }

// 0x11 WRITE_PARAM —— 写入全部配置（DATA = config_t 231B，仅 RAM）
// 便捷重载 bin_write_param(const Config&) 见 §6.5（依赖 Config 声明）
inline std::vector<uint8_t> bin_write_param(const std::vector<uint8_t>& cfg) {
    if (cfg.size() != MD_CONFIG_SIZE)
        throw std::invalid_argument("mdc::bin_write_param: config_t 必须为 231B");
    return build_frame(MD_CMD_WRITE_PARAM, cfg);
}

// 0x12 WRITE_FIELD —— 按偏移写单个字段 [field_id:2B LE][value:nB]
// 协议规范：offset < 12（受保护区）拒绝写入
inline std::vector<uint8_t> bin_write_field(uint16_t field_id,
                                            const std::vector<uint8_t>& value) {
    if (field_id < 12)
        throw std::invalid_argument(
            "mdc::bin_write_field: 受保护区 (offset<12) 不可写入");
    std::vector<uint8_t> data;
    data.reserve(2 + value.size());
    detail::wr_u16_append(data, field_id);
    data.insert(data.end(), value.begin(), value.end());
    return build_frame(MD_CMD_WRITE_FIELD, data);
}

// 0x20 SAVE_EEPROM —— RAM 配置写入 EEPROM
inline std::vector<uint8_t> bin_save() { return build_frame(MD_CMD_SAVE_EEPROM); }

// 0x21 LOAD_EEPROM —— 从 EEPROM 加载到 RAM
inline std::vector<uint8_t> bin_load() { return build_frame(MD_CMD_LOAD_EEPROM); }

// 0x22 FACTORY_RESET —— 恢复出厂默认（RAM+EEPROM）
inline std::vector<uint8_t> bin_factory_reset() {
    return build_frame(MD_CMD_FACTORY_RESET);
}

// 0x30 MOTOR_RAW —— [ch:1B][dir:1B][pwm:2B LE]
// ch=0~3；dir=0 正转/1 反转；pwm=0~1000（越界抛 std::invalid_argument）
inline std::vector<uint8_t> bin_motor_raw(uint8_t ch, uint8_t dir, uint16_t pwm) {
    if (ch > 3)
        throw std::invalid_argument("mdc::bin_motor_raw: ch 取值 0~3");
    if (dir > 1)
        throw std::invalid_argument("mdc::bin_motor_raw: dir 取值 0(正)/1(反)");
    if (pwm > 1000)
        throw std::invalid_argument("mdc::bin_motor_raw: pwm 取值 0~1000");
    std::vector<uint8_t> data{ch, dir};
    detail::wr_u16_append(data, pwm);
    return build_frame(MD_CMD_MOTOR_RAW, data);
}

// 0x31 MOTOR_CTRL —— [m1~m4:4×int32 LE]（核心控制帧）
// 含义随各通道模式：开环=PWM(±1000) 速度=RPM 位置=0.1°
inline std::vector<uint8_t> bin_motor_ctrl(int32_t t0, int32_t t1,
                                           int32_t t2, int32_t t3) {
    std::vector<uint8_t> data;
    data.reserve(16);
    detail::wr_i32_append(data, t0);
    detail::wr_i32_append(data, t1);
    detail::wr_i32_append(data, t2);
    detail::wr_i32_append(data, t3);
    return build_frame(MD_CMD_MOTOR_CTRL, data);
}

// 0x40 SUBSCRIBE —— [interval_ms:2B LE]（固件钳位 ≥20ms）
inline std::vector<uint8_t> bin_subscribe(uint16_t interval_ms) {
    std::vector<uint8_t> data;
    detail::wr_u16_append(data, interval_ms);
    return build_frame(MD_CMD_SUBSCRIBE, data);
}

// 0x41 UNSUBSCRIBE —— 关闭状态上报
inline std::vector<uint8_t> bin_unsubscribe() {
    return build_frame(MD_CMD_UNSUBSCRIBE);
}

// 0x43 DEBUG_SBUS —— [enable:1B] SBUS 16 通道实时上报开关
inline std::vector<uint8_t> bin_debug_sbus(uint8_t enable) {
    return build_frame(MD_CMD_DEBUG_SBUS, std::vector<uint8_t>{enable});
}

// 0x44 DEBUG_SPEED —— [enable:1B] 速度原始值上报开关（0xF0 帧扩展为 72B）
inline std::vector<uint8_t> bin_debug_speed(uint8_t enable) {
    return build_frame(MD_CMD_DEBUG_SPEED, std::vector<uint8_t>{enable});
}

// 0x52 ENTER_BL —— 软复位进入 Bootloader
inline std::vector<uint8_t> bin_enter_bl() { return build_frame(MD_CMD_ENTER_BL); }

// 0x53 REBOOT —— 系统重启（ACK 后延迟 100ms）
inline std::vector<uint8_t> bin_reboot() { return build_frame(MD_CMD_REBOOT); }

// ============================================================================
// §6 解析层
// ============================================================================

// §6.1 ACK 解析 —— 输入 ACK 帧的 DATA 段（1 字节 err）
//      err=0x00 成功；任何非 0（0xFF/0x01/0x02...）均视为失败。
//      注意：DATA 段本身不含命令号，out.cmd 恒为 0，调用方从帧头自行获取。
struct Ack {
    uint8_t cmd = 0;       // 命令号（DATA 段不可推导，恒为 0）
    uint8_t err = MD_ERR_OK;
    bool ok() const { return err == MD_ERR_OK; }   // 便捷：是否成功
};
inline bool parse_ack(const uint8_t* payload, size_t len, Ack& out) {
    if (payload == nullptr || len != 1) return false;
    out.cmd = 0;
    out.err = payload[0];
    return true;
}
inline bool parse_ack(const std::vector<uint8_t>& payload, Ack& out) {
    return parse_ack(payload.data(), payload.size(), out);
}

// §6.2 STATUS_REPORT 解析 —— 56B（常规）/ 72B（扩展）按 len 自动兼容
struct Status {
    std::array<int32_t, 4> enc{};        // @0   编码器累计脉冲
    std::array<float, 4>   tgt{};        // @16  当前目标值（PWM/RPM/0.1°）
    std::array<int32_t, 4> rpm{};        // @32  滤波后实时转速（RPM）
    std::array<int32_t, 4> rpm_raw{};    // @48  滤波前原始 RPM（72B 有效；56B 为 0）
    uint32_t sbus_frame_cnt = 0;         // 56B:@48 / 72B:@64 SBUS 帧组装计数
    uint32_t sbus_ok_cnt    = 0;         // 56B:@52 / 72B:@68 SBUS 校验通过计数
    uint8_t  extended = 0;               // 1=72B 扩展模式
};
inline bool parse_status(const uint8_t* payload, size_t len, Status& out) {
    if (payload == nullptr) return false;
    Status st;
    if (len == 56) {
        for (int i = 0; i < 4; ++i) {
            st.enc[i]     = detail::rd_i32(payload + 0  + i * 4);
            st.tgt[i]     = detail::rd_f32(payload + 16 + i * 4);
            st.rpm[i]     = detail::rd_i32(payload + 32 + i * 4);
            st.rpm_raw[i] = 0;                          // 56B 模式无原始值
        }
        st.sbus_frame_cnt = detail::rd_u32(payload + 48);
        st.sbus_ok_cnt    = detail::rd_u32(payload + 52);
        st.extended = 0;
    } else if (len == 72) {
        for (int i = 0; i < 4; ++i) {
            st.enc[i]     = detail::rd_i32(payload + 0  + i * 4);
            st.tgt[i]     = detail::rd_f32(payload + 16 + i * 4);
            st.rpm[i]     = detail::rd_i32(payload + 32 + i * 4);
            st.rpm_raw[i] = detail::rd_i32(payload + 48 + i * 4);
        }
        st.sbus_frame_cnt = detail::rd_u32(payload + 64);
        st.sbus_ok_cnt    = detail::rd_u32(payload + 68);
        st.extended = 1;
    } else {
        return false;   // 非法长度（应为 56 或 72）
    }
    out = st;
    return true;
}
inline bool parse_status(const std::vector<uint8_t>& payload, Status& out) {
    return parse_status(payload.data(), payload.size(), out);
}

// §6.3 DETECT_REPORT 解析 —— [proto:1B][inv:1B][baud:4B LE]
//      proto：0=失败 1=SBUS 2=UART 3=ELRS
struct Detect {
    uint8_t  proto = 0;   // 协议类型
    uint8_t  inv = 0;     // 信号反相标志
    uint32_t baud = 0;    // 检测到的波特率
};
inline bool parse_detect(const uint8_t* payload, size_t len, Detect& out) {
    if (payload == nullptr || len != 6) return false;
    Detect d;
    d.proto = payload[0];
    d.inv   = payload[1];
    d.baud  = detail::rd_u32(payload + 2);
    out = d;
    return true;
}
inline bool parse_detect(const std::vector<uint8_t>& payload, Detect& out) {
    return parse_detect(payload.data(), payload.size(), out);
}

// §6.4 SBUS_DATA 解析 —— [ch0~15:16×uint16 LE]
inline bool parse_sbus(const uint8_t* payload, size_t len,
                       std::array<uint16_t, 16>& out) {
    if (payload == nullptr || len != 32) return false;
    std::array<uint16_t, 16> ch{};
    for (int i = 0; i < 16; ++i) ch[static_cast<size_t>(i)] =
        detail::rd_u16(payload + i * 2);
    out = ch;
    return true;
}
inline bool parse_sbus(const std::vector<uint8_t>& payload,
                       std::array<uint16_t, 16>& out) {
    return parse_sbus(payload.data(), payload.size(), out);
}

// §6.5 config_t —— md_config_t（协议规范 §5 偏移表，全字段含位域）
#if MD_ENABLE_CONFIG
struct Config {
    /* ── 通讯 ── */
    uint32_t baud_rate = 0;          // @11 USART2 波特率
    uint16_t cmd_timeout_ms = 0;     // @15 指令超时保护（ms，0=关闭）
    uint8_t  protocol = 0;           // comm_flags bit0-3: 1=SBUS 2=UART 3=ELRS
    uint8_t  sbus_inv = 0;           // comm_flags bit4
    uint8_t  ctrl_priority = 0;      // comm_flags bit5: 0=USART2 优先 1=USB 优先
    /* ── 电机 ×4 ── */
    std::array<uint8_t, 4>  control_mode{};       // @18 每电机2bit: 0开环 1速度 2位置
    std::array<uint8_t, 4>  motor_invert{};       // @19 每电机2bit: bit0引脚反转 bit1编码器极性
    std::array<uint16_t, 4> encoder_cpr{};        // @20 编码器线数
    std::array<uint16_t, 4> speed_period_ms{};    // @28 速度环周期（ms）
    std::array<uint8_t, 4>  speed_pid_type{};     // @36 每电机4bit: 0位置式 1增量式
    std::array<uint16_t, 4> speed_olim{};         // @38 速度环输出限幅（PWM 0~1000）
    std::array<float, 4>    speed_kp{};           // @46 速度环 Kp
    std::array<float, 4>    speed_ki{};           //     Ki
    std::array<float, 4>    speed_kd{};           //     Kd
    std::array<float, 4>    speed_ilim{};         //     Ilim（积分限幅）
    std::array<uint16_t, 4> pos_period_ms{};      // @110 位置环周期（ms）
    std::array<uint8_t, 4>  pos_pid_type{};       // @118 每电机4bit
    std::array<float, 4>    pos_kp{};             // @120 位置环 Kp
    std::array<float, 4>    pos_ki{};             //     Ki
    std::array<float, 4>    pos_kd{};             //     Kd
    std::array<float, 4>    pos_ilim{};           //     Ilim
    std::array<float, 4>    pos_olim{};           // @184 位置环输出限幅（RPM）
    std::array<uint16_t, 4> pos_angle_cpr{};      // @200 位置环转一圈脉冲数（0=用 encoder_cpr）
    std::array<uint8_t, 4>  speed_filter_type{};  // @208 每电机4bit: 0无 1滑动平均 2低通 3中值
    std::array<uint8_t, 4>  speed_filter_window{};// @210 滤波窗口
    std::array<uint8_t, 4>  sbus_channel{};       // @214 遥控通道映射：结构体用 1~16（CH1~16）
    std::array<uint8_t, 4>  rc_dir_ch{};          // @216 方向映射通道：结构体用 1~16
    std::array<uint8_t, 4>  rc_map_mode{};        // @218 bit0-3 每电机1bit: 0中心零点 1min零点
    std::array<uint8_t, 4>  rc_dir_en{};          // @218 bit4-7 每电机1bit: 方向映射使能
    std::array<uint16_t, 4> sbus_param{};         // @219 遥控行程
    uint16_t sbus_range_min = 0;                  // @227 通道值下边界
    uint16_t sbus_range_max = 0;                  // @229 通道值上边界
};

// 解析 231B config_t → Config（失败返回 false，输出参数不变）
inline bool parse_config(const uint8_t* raw, size_t len, Config& out) {
    if (raw == nullptr || len != MD_CONFIG_SIZE) return false;
    Config c;
    c.baud_rate      = detail::rd_u32(raw + 11);
    c.cmd_timeout_ms = detail::rd_u16(raw + 15);
    const uint8_t flags = raw[17];                 // comm_flags
    c.protocol      = static_cast<uint8_t>(flags & 0x0F);
    c.sbus_inv      = static_cast<uint8_t>((flags >> 4) & 0x01);
    c.ctrl_priority = static_cast<uint8_t>((flags >> 5) & 0x01);
    for (int i = 0; i < 4; ++i) {
        const size_t ci = static_cast<size_t>(i);
        c.control_mode[ci] = static_cast<uint8_t>((raw[18] >> (i * 2)) & 0x03);
        c.motor_invert[ci] = static_cast<uint8_t>((raw[19] >> (i * 2)) & 0x03);
        c.encoder_cpr[ci]     = detail::rd_u16(raw + 20  + i * 2);
        c.speed_period_ms[ci] = detail::rd_u16(raw + 28  + i * 2);
        c.speed_olim[ci]      = detail::rd_u16(raw + 38  + i * 2);
        c.speed_kp[ci]    = detail::rd_f32(raw + 46 + i * 16 + 0);
        c.speed_ki[ci]    = detail::rd_f32(raw + 46 + i * 16 + 4);
        c.speed_kd[ci]    = detail::rd_f32(raw + 46 + i * 16 + 8);
        c.speed_ilim[ci]  = detail::rd_f32(raw + 46 + i * 16 + 12);
        c.pos_period_ms[ci] = detail::rd_u16(raw + 110 + i * 2);
        c.pos_kp[ci]    = detail::rd_f32(raw + 120 + i * 16 + 0);
        c.pos_ki[ci]    = detail::rd_f32(raw + 120 + i * 16 + 4);
        c.pos_kd[ci]    = detail::rd_f32(raw + 120 + i * 16 + 8);
        c.pos_ilim[ci]  = detail::rd_f32(raw + 120 + i * 16 + 12);
        c.pos_olim[ci]      = detail::rd_f32(raw + 184 + i * 4);
        c.pos_angle_cpr[ci] = detail::rd_u16(raw + 200 + i * 2);
        c.speed_filter_window[ci] = raw[210 + i];
        // sbus_channel/rc_dir_ch：存储值 0~15 = CH1~16，结构体用 1~16（解析 +1）
        c.sbus_channel[ci] = static_cast<uint8_t>(
            ((detail::rd_u16(raw + 214) >> (i * 4)) & 0x0F) + 1);
        c.rc_dir_ch[ci] = static_cast<uint8_t>(
            ((detail::rd_u16(raw + 216) >> (i * 4)) & 0x0F) + 1);
        c.rc_map_mode[ci] = static_cast<uint8_t>((raw[218] >> i) & 0x01);
        c.rc_dir_en[ci]   = static_cast<uint8_t>((raw[218] >> (4 + i)) & 0x01);
        c.sbus_param[ci]  = detail::rd_u16(raw + 219 + i * 2);
    }
    // 每电机 4bit 打包字段（u16）
    const uint16_t spd_type = detail::rd_u16(raw + 36);
    const uint16_t pos_type = detail::rd_u16(raw + 118);
    const uint16_t flt_type = detail::rd_u16(raw + 208);
    for (int i = 0; i < 4; ++i) {
        c.speed_pid_type[static_cast<size_t>(i)] =
            static_cast<uint8_t>((spd_type >> (i * 4)) & 0x0F);
        c.pos_pid_type[static_cast<size_t>(i)] =
            static_cast<uint8_t>((pos_type >> (i * 4)) & 0x0F);
        c.speed_filter_type[static_cast<size_t>(i)] =
            static_cast<uint8_t>((flt_type >> (i * 4)) & 0x0F);
    }
    c.sbus_range_min = detail::rd_u16(raw + 227);
    c.sbus_range_max = detail::rd_u16(raw + 229);
    out = c;
    return true;
}
inline bool parse_config(const std::vector<uint8_t>& raw, Config& out) {
    return parse_config(raw.data(), raw.size(), out);
}

// 打包 Config → 231B config_t（受保护区 offset 0~10 置 0，固件写入时自动还原）
inline std::vector<uint8_t> pack_config(const Config& cfg) {
    std::vector<uint8_t> raw(MD_CONFIG_SIZE, 0);     // 受保护区全部置 0
    detail::wr_u32(raw, 11, cfg.baud_rate);
    detail::wr_u16(raw, 15, cfg.cmd_timeout_ms);
    // comm_flags 位域
    raw[17] = static_cast<uint8_t>(
        (cfg.protocol & 0x0F) |
        ((cfg.sbus_inv & 0x01) << 4) |
        ((cfg.ctrl_priority & 0x01) << 5));
    // 每电机 2bit 打包字段
    uint8_t cm = 0, mi = 0;
    for (int i = 0; i < 4; ++i) {
        cm = static_cast<uint8_t>(cm | ((cfg.control_mode[static_cast<size_t>(i)] & 0x03) << (i * 2)));
        mi = static_cast<uint8_t>(mi | ((cfg.motor_invert[static_cast<size_t>(i)] & 0x03) << (i * 2)));
    }
    raw[18] = cm;
    raw[19] = mi;
    // 每电机 4bit 打包字段（u16）
    uint16_t spd_type = 0, pos_type = 0, flt_type = 0;
    uint16_t sbus_ch = 0, rc_dir = 0;
    for (int i = 0; i < 4; ++i) {
        spd_type = static_cast<uint16_t>(spd_type | ((cfg.speed_pid_type[static_cast<size_t>(i)] & 0x0F) << (i * 4)));
        pos_type = static_cast<uint16_t>(pos_type | ((cfg.pos_pid_type[static_cast<size_t>(i)] & 0x0F) << (i * 4)));
        flt_type = static_cast<uint16_t>(flt_type | ((cfg.speed_filter_type[static_cast<size_t>(i)] & 0x0F) << (i * 4)));
        // 结构体 1~16 → 存储 0~15（打包 -1）
        sbus_ch = static_cast<uint16_t>(sbus_ch | (((cfg.sbus_channel[static_cast<size_t>(i)] - 1) & 0x0F) << (i * 4)));
        rc_dir  = static_cast<uint16_t>(rc_dir  | (((cfg.rc_dir_ch[static_cast<size_t>(i)] - 1) & 0x0F) << (i * 4)));
    }
    detail::wr_u16(raw, 36,  spd_type);
    detail::wr_u16(raw, 118, pos_type);
    detail::wr_u16(raw, 208, flt_type);
    detail::wr_u16(raw, 214, sbus_ch);
    detail::wr_u16(raw, 216, rc_dir);
    // rc_map_mode：低4bit=映射模式，高4bit=方向映射使能
    uint8_t rmm = 0;
    for (int i = 0; i < 4; ++i) {
        rmm = static_cast<uint8_t>(rmm |
            ((cfg.rc_map_mode[static_cast<size_t>(i)] & 0x01) << i) |
            ((cfg.rc_dir_en[static_cast<size_t>(i)] & 0x01) << (4 + i)));
    }
    raw[218] = rmm;
    // 逐通道数组字段
    for (int i = 0; i < 4; ++i) {
        const size_t ci = static_cast<size_t>(i);
        detail::wr_u16(raw, 20  + i * 2, cfg.encoder_cpr[ci]);
        detail::wr_u16(raw, 28  + i * 2, cfg.speed_period_ms[ci]);
        detail::wr_u16(raw, 38  + i * 2, cfg.speed_olim[ci]);
        detail::wr_f32(raw, 46 + i * 16 + 0,  cfg.speed_kp[ci]);
        detail::wr_f32(raw, 46 + i * 16 + 4,  cfg.speed_ki[ci]);
        detail::wr_f32(raw, 46 + i * 16 + 8,  cfg.speed_kd[ci]);
        detail::wr_f32(raw, 46 + i * 16 + 12, cfg.speed_ilim[ci]);
        detail::wr_u16(raw, 110 + i * 2, cfg.pos_period_ms[ci]);
        detail::wr_f32(raw, 120 + i * 16 + 0,  cfg.pos_kp[ci]);
        detail::wr_f32(raw, 120 + i * 16 + 4,  cfg.pos_ki[ci]);
        detail::wr_f32(raw, 120 + i * 16 + 8,  cfg.pos_kd[ci]);
        detail::wr_f32(raw, 120 + i * 16 + 12, cfg.pos_ilim[ci]);
        detail::wr_f32(raw, 184 + i * 4, cfg.pos_olim[ci]);
        detail::wr_u16(raw, 200 + i * 2, cfg.pos_angle_cpr[ci]);
        raw[210 + i] = cfg.speed_filter_window[ci];
        detail::wr_u16(raw, 219 + i * 2, cfg.sbus_param[ci]);
    }
    detail::wr_u16(raw, 227, cfg.sbus_range_min);
    detail::wr_u16(raw, 229, cfg.sbus_range_max);
    return raw;
}

// 便捷重载：直接传 Config 结构体（内部 pack_config），定义于此因依赖 Config
inline std::vector<uint8_t> bin_write_param(const Config& cfg) {
    return bin_write_param(pack_config(cfg));
}
#endif // MD_ENABLE_CONFIG

} // namespace mdc

#endif // MDC_LIB_HPP
