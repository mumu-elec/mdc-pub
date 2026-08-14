#ifndef MOTOR_DRIVER_HPP
#define MOTOR_DRIVER_HPP

// ============================================================================
// motor_driver.hpp — 二进制帧协议封装类（头文件实现，直接 #include 使用）
//
// 帧格式: [SYNC=0xAA][CMD][LEN][DATA...][CRC8]
//   CRC8 : 多项式 0x07，初值 0，计算范围 = CMD+LEN+DATA（不含 SYNC）
//   ACK  : 0xAA + CMD + 0x01 + err + CRC8，err=0x00 成功 / 0xFF 失败
// 多字节字段一律小端序（LE）。
//
// 命令常量与 firmware v1.2.0 的 binary_proto.h 保持一致。
// ============================================================================

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "serial_port.h"

namespace mdc {

// ── 帧与命令常量 ──
constexpr uint8_t SYNC = 0xAA;

constexpr uint8_t CMD_PING           = 0x01;
constexpr uint8_t CMD_READ_PARAM     = 0x10;   // 应答 config_t 231B
constexpr uint8_t CMD_WRITE_PARAM    = 0x11;
constexpr uint8_t CMD_WRITE_FIELD    = 0x12;
constexpr uint8_t CMD_SAVE_EEPROM    = 0x20;
constexpr uint8_t CMD_LOAD_EEPROM    = 0x21;
constexpr uint8_t CMD_FACTORY_RESET  = 0x22;
constexpr uint8_t CMD_MOTOR_RAW      = 0x30;   // 单通道 PWM 直驱（无应答）
constexpr uint8_t CMD_MOTOR_CTRL     = 0x31;   // 四通道批量控制（无应答）
constexpr uint8_t CMD_SUBSCRIBE      = 0x40;
constexpr uint8_t CMD_UNSUBSCRIBE    = 0x41;
constexpr uint8_t CMD_DEBUG_SBUS     = 0x43;
constexpr uint8_t CMD_DEBUG_SPEED    = 0x44;
constexpr uint8_t CMD_ENTER_BL       = 0x52;
constexpr uint8_t CMD_REBOOT         = 0x53;
constexpr uint8_t CMD_STATUS_REPORT  = 0xF0;   // MCU 主动推送
constexpr uint8_t CMD_DETECT_REPORT  = 0xF1;
constexpr uint8_t CMD_SBUS_DATA      = 0xF2;

// config_t 大小（布局 v2.1）
constexpr size_t CONFIG_SIZE = 231;

} // namespace mdc

// 一帧解析结果
struct MdcFrame {
    uint8_t cmd = 0;
    std::vector<uint8_t> payload;               // DATA 段（不含 SYNC/CMD/LEN/CRC）
};

// MotorDriver: 基于 SerialPort 的二进制协议封装
class MotorDriver {
public:
    explicit MotorDriver(SerialPort& sp) : sp_(sp) {}

    // ── 基础工具 ──
    // CRC8: 多项式 0x07，初值 0，逐位实现（与协议规范参考实现等价）
    static uint8_t crc8(const uint8_t* d, size_t len) {
        uint8_t c = 0;
        for (size_t i = 0; i < len; i++) {
            c ^= d[i];
            for (int b = 0; b < 8; b++)
                c = (c & 0x80) ? static_cast<uint8_t>((c << 1) ^ 0x07)
                               : static_cast<uint8_t>(c << 1);
        }
        return c;
    }

    // 组帧: [0xAA, cmd, len, data..., crc8(cmd+len+data)]
    static std::vector<uint8_t> buildFrame(uint8_t cmd, const std::vector<uint8_t>& data) {
        std::vector<uint8_t> f;
        f.reserve(4 + data.size());
        f.push_back(mdc::SYNC);
        f.push_back(cmd);
        f.push_back(static_cast<uint8_t>(data.size()));
        f.insert(f.end(), data.begin(), data.end());
        f.push_back(crc8(&f[1], 2 + data.size()));      // CMD+LEN+DATA
        return f;
    }

    // 发送整帧；返回是否写成功
    bool sendFrame(uint8_t cmd, const std::vector<uint8_t>& data) {
        const std::vector<uint8_t> f = buildFrame(cmd, data);
        return sp_.write(f.data(), f.size()) >= 0;
    }

    // 读取一帧（滑动窗口找 0xAA → 按 LEN 收齐 → CRC8 校验）
    // 成功返回 true 并填充 out；超时/出错返回 false。
    // 内部保留 rxBuf_，支持跨多次调用重组帧（订阅模式下帧是连续推流）。
    bool readFrame(int timeout_ms, MdcFrame& out) {
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::milliseconds(timeout_ms);

        while (std::chrono::steady_clock::now() < deadline) {
            // 滑动窗口在 rxBuf_ 中找完整帧
            for (;;) {
                auto it = std::find(rxBuf_.begin(), rxBuf_.end(), mdc::SYNC);
                if (it == rxBuf_.end()) {
                    rxBuf_.clear();                     // 无 SYNC，清空等待新数据
                    break;
                }
                if (it != rxBuf_.begin())
                    rxBuf_.erase(rxBuf_.begin(), it);    // 丢弃 SYNC 前的杂散字节

                if (rxBuf_.size() < 3) break;            // 帧头未齐
                const size_t len   = rxBuf_[2];
                const size_t need  = 3 + len + 1;        // SYNC+CMD+LEN+DATA+CRC
                if (rxBuf_.size() < need) break;         // 数据未收齐，先补读

                if (crc8(&rxBuf_[1], 2 + len) == rxBuf_[need - 1]) {
                    out.cmd = rxBuf_[1];
                    out.payload.assign(rxBuf_.begin() + 3, rxBuf_.begin() + 3 + len);
                    rxBuf_.erase(rxBuf_.begin(), rxBuf_.begin() + need);
                    return true;
                }
                rxBuf_.erase(rxBuf_.begin());            // CRC 失败：丢弃该 SYNC 继续找
            }

            // 补读串口数据
            const auto now = std::chrono::steady_clock::now();
            const int remain = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
            if (remain <= 0) break;
            uint8_t tmp[256];
            const int n = sp_.read(tmp, sizeof(tmp), remain < 200 ? remain : 200);
            if (n < 0) return false;                     // 串口出错
            if (n > 0) rxBuf_.insert(rxBuf_.end(), tmp, tmp + n);
        }
        return false;                                    // 整体超时
    }

    // 读取 ACK 帧并校验：CMD 一致、LEN==1、err ∈ {0x00, 0xFF}。
    // 返回 true 表示 err==0x00（设备成功）；false 表示超时/帧异常/设备报错。
    // err 码通过 outErr 透出（0xFF = 设备返回失败，可据此区分超时）。
    bool readAck(uint8_t cmd, int timeout_ms, uint8_t* outErr = nullptr) {
        MdcFrame f;
        if (!readFrame(timeout_ms, f)) return false;
        if (f.cmd != cmd || f.payload.size() != 1) return false;
        if (outErr) *outErr = f.payload[0];
        return f.payload[0] == 0x00;
    }

    // ── 命令封装 ──

    // PING 连通性测试
    bool ping(int timeout_ms = 500) {
        return sendFrame(mdc::CMD_PING, {}) && readAck(mdc::CMD_PING, timeout_ms);
    }

    // 读取全部配置（config_t 231B）
    bool readParam(std::vector<uint8_t>& cfg, int timeout_ms = 1000) {
        if (!sendFrame(mdc::CMD_READ_PARAM, {})) return false;
        MdcFrame f;
        if (!readFrame(timeout_ms, f)) return false;
        if (f.cmd != mdc::CMD_READ_PARAM || f.payload.size() != mdc::CONFIG_SIZE)
            return false;
        cfg = std::move(f.payload);
        return true;
    }

    // 写入全部配置（仅 RAM；受保护字段 offset 0~10 由固件自动还原）
    bool writeParam(const std::vector<uint8_t>& cfg, int timeout_ms = 1000) {
        if (cfg.size() != mdc::CONFIG_SIZE) return false;
        return sendFrame(mdc::CMD_WRITE_PARAM, cfg) &&
               readAck(mdc::CMD_WRITE_PARAM, timeout_ms);
    }

    // 按偏移写单个字段（offset < 12 的受保护字段会被固件拒绝）
    bool writeField(uint16_t offset, const std::vector<uint8_t>& value, int timeout_ms = 1000) {
        std::vector<uint8_t> data(2 + value.size());
        data[0] = static_cast<uint8_t>(offset & 0xFF);
        data[1] = static_cast<uint8_t>((offset >> 8) & 0xFF);
        std::copy(value.begin(), value.end(), data.begin() + 2);
        return sendFrame(mdc::CMD_WRITE_FIELD, data) &&
               readAck(mdc::CMD_WRITE_FIELD, timeout_ms);
    }

    // RAM 配置写入 EEPROM（约 190ms，超时给足余量）
    bool save(int timeout_ms = 1500) {
        return sendFrame(mdc::CMD_SAVE_EEPROM, {}) && readAck(mdc::CMD_SAVE_EEPROM, timeout_ms);
    }

    // 从 EEPROM 加载配置到 RAM
    bool load(int timeout_ms = 1000) {
        return sendFrame(mdc::CMD_LOAD_EEPROM, {}) && readAck(mdc::CMD_LOAD_EEPROM, timeout_ms);
    }

    // 恢复出厂默认（RAM + EEPROM）
    bool factoryReset(int timeout_ms = 1000) {
        return sendFrame(mdc::CMD_FACTORY_RESET, {}) &&
               readAck(mdc::CMD_FACTORY_RESET, timeout_ms);
    }

    // 单通道 PWM 直驱（旁路 PID）。ch=0~3；dir=0 正转 / 1 反转；pwm=0~1000。
    // 无应答帧。受优先级仲裁（USB 主控需先 /priority 1）。
    bool motorRaw(uint8_t ch, uint8_t dir, uint16_t pwm) {
        std::vector<uint8_t> data = {
            ch, dir,
            static_cast<uint8_t>(pwm & 0xFF),
            static_cast<uint8_t>((pwm >> 8) & 0xFF)
        };
        return sendFrame(mdc::CMD_MOTOR_RAW, data);
    }

    // 四通道批量控制（核心控制帧）。targets[4] 为 int32 小端：
    // 开环=PWM(±1000) / 速度=RPM / 位置=0.1°(±3600)。无应答帧，受优先级仲裁。
    bool motorCtrl(const int32_t targets[4]) {
        std::vector<uint8_t> data(16);
        for (int i = 0; i < 4; i++) {
            const uint32_t v = static_cast<uint32_t>(targets[i]);
            data[i * 4 + 0] = static_cast<uint8_t>(v & 0xFF);
            data[i * 4 + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
            data[i * 4 + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
            data[i * 4 + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
        }
        return sendFrame(mdc::CMD_MOTOR_CTRL, data);
    }

    // 开启状态周期上报（最低 20ms）
    bool subscribe(uint16_t interval_ms, int timeout_ms = 500) {
        std::vector<uint8_t> data = {
            static_cast<uint8_t>(interval_ms & 0xFF),
            static_cast<uint8_t>((interval_ms >> 8) & 0xFF)
        };
        return sendFrame(mdc::CMD_SUBSCRIBE, data) && readAck(mdc::CMD_SUBSCRIBE, timeout_ms);
    }

    // 关闭状态上报
    bool unsubscribe(int timeout_ms = 500) {
        return sendFrame(mdc::CMD_UNSUBSCRIBE, {}) && readAck(mdc::CMD_UNSUBSCRIBE, timeout_ms);
    }

    // SBUS 16 通道实时上报开关（0xF2 帧）
    bool debugSbus(bool enable, int timeout_ms = 500) {
        std::vector<uint8_t> data = { static_cast<uint8_t>(enable ? 1 : 0) };
        return sendFrame(mdc::CMD_DEBUG_SBUS, data) && readAck(mdc::CMD_DEBUG_SBUS, timeout_ms);
    }

    // 速度原始值上报开关（STATUS_REPORT 扩展为 72B）
    bool debugSpeed(bool enable, int timeout_ms = 500) {
        std::vector<uint8_t> data = { static_cast<uint8_t>(enable ? 1 : 0) };
        return sendFrame(mdc::CMD_DEBUG_SPEED, data) && readAck(mdc::CMD_DEBUG_SPEED, timeout_ms);
    }

    // 系统重启（ACK 后延迟 100ms 复位；若收不到 ACK 也属正常）
    bool reboot(int timeout_ms = 1000) {
        return sendFrame(mdc::CMD_REBOOT, {}) && readAck(mdc::CMD_REBOOT, timeout_ms);
    }

private:
    SerialPort& sp_;
    std::vector<uint8_t> rxBuf_;                // 帧重组缓冲（跨调用保留）
};

#endif // MOTOR_DRIVER_HPP
