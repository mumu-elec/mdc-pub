// ============================================================================
// 例程 03: binary_protocol —— mdc_lib 二进制 API 调用指南
//
// 流程: 打开串口 → PING(0x01)：mdc::bin_ping() 打包发送，
//       mdc::Parser 流式解析 ACK → READ_PARAM(0x10)：mdc::bin_read_param()
//       发送并接收 231B 应答，mdc::parse_config 解析打印版本与关键字段
//       → mdc::bin_motor_ctrl 打包示例（仅演示打包，不发送）
//
// 说明: 本工程不再包含任何协议实现（CRC8 / 组帧 / 帧解析 / 字节序），
//       全部调用 mdc_lib（命名空间 mdc），串口收发由 serial_port 完成。
//
// 用法: binary_protocol <串口>
//   Windows: binary_protocol COM3
//   Linux:   binary_protocol /dev/ttyUSB0
// ============================================================================

#include <chrono>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "mdc_lib.hpp"      // mdc_lib：bin_ping / bin_read_param / Parser / parse_ack / parse_config ...
#include "serial_port.h"    // 串口收发（用户实现）

static void printHex(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) std::printf("%02X ", p[i]);
    std::printf("\n");
}

// 读取一帧：串口收到的字节逐字节喂给 mdc::Parser（自动找 0xAA 同步字 +
// CRC 校验，文本噪声自动丢弃）。收齐一帧返回 true 并输出 cmd/payload；
// 整体超时返回 false。
static bool readFrame(SerialPort& sp, mdc::Parser& parser, int timeoutMs,
                      uint8_t& cmd, std::vector<uint8_t>& payload) {
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        uint8_t buf[256];
        const int n = sp.read(buf, sizeof(buf), 100);
        if (n < 0) return false;                        // 串口出错
        for (int i = 0; i < n; ++i) {
            auto r = parser.feed(buf[i]);               // 逐字节喂给流式解析器
            if (r) {
                cmd = r->first;
                payload = std::move(r->second);
                return true;
            }
        }
    }
    return false;                                       // 整体超时
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::printf("用法: %s <串口>\n", argv[0]);
        std::printf("  Windows 示例: %s COM3\n", argv[0]);
        std::printf("  Linux   示例: %s /dev/ttyUSB0\n", argv[0]);
        return 1;
    }

    SerialPort sp;
    std::string err;
    if (!sp.open(argv[1], 2000000, &err)) {
        std::printf("[错误] 打开串口失败: %s\n", err.c_str());
        return 1;
    }
    std::printf("== 已打开 %s @ 2000000-8N1 ==\n", argv[1]);

    // ── 1) PING：mdc::bin_ping() 打包 → 发送 → mdc::Parser 收帧 → mdc::parse_ack ──
    std::printf("== PING (0x01) ==\n");
    mdc::Parser parser;
    const std::vector<uint8_t> pingFrame = mdc::bin_ping();
    if (sp.write(pingFrame.data(), pingFrame.size()) < 0) {
        std::printf("[错误] 发送失败: %s\n", sp.lastError().c_str());
        sp.close();
        return 1;
    }
    uint8_t cmd = 0;
    std::vector<uint8_t> payload;
    if (readFrame(sp, parser, 1000, cmd, payload) && cmd == mdc::MD_CMD_PING) {
        mdc::Ack ack;
        if (mdc::parse_ack(payload, ack) && ack.ok()) {
            std::printf("  OK: 设备在线，ACK err=0x00\n");
        } else {
            std::printf("  失败: ACK err=0x%02X\n", ack.err);
        }
    } else {
        std::printf("  失败: 无 ACK 应答\n");
    }

    // ── 2) READ_PARAM：读取 config_t(231B) ──
    std::printf("== READ_PARAM (0x10) ==\n");
    const std::vector<uint8_t> readParamFrame = mdc::bin_read_param();
    if (sp.write(readParamFrame.data(), readParamFrame.size()) < 0) {
        std::printf("[错误] 发送失败: %s\n", sp.lastError().c_str());
        sp.close();
        return 1;
    }
    std::vector<uint8_t> raw;
    if (readFrame(sp, parser, 1000, cmd, payload) && cmd == mdc::MD_CMD_READ_PARAM
        && payload.size() == mdc::MD_CONFIG_SIZE) {
        raw = std::move(payload);
    } else {
        std::printf("  读取失败（无应答或 LEN != 231）\n");
        sp.close();
        return 1;
    }
    std::printf("  收到 %zu 字节 (期望 231)\n", raw.size());

    // ── 3) 打印前 16 字节 HEX ──
    std::printf("  前 16 字节 HEX: ");
    printHex(raw.data(), raw.size() < 16 ? raw.size() : 16);

    // ── 4) 版本信息（config_t 头部 offset 0~10 为受保护区，按字节直接显示）──
    std::printf("== 版本信息 (offset 0~10) ==\n");
    std::printf("  magic      [0-3]  = %02X %02X %02X %02X %s\n",
                raw[0], raw[1], raw[2], raw[3],
                (raw[0] == 0x00 && raw[1] == 'R' && raw[2] == 'D' && raw[3] == 'M')
                    ? "(正确)" : "(异常)");
    std::printf("  hw_ver     [4-6]  = %u.%u.%u\n", raw[4], raw[5], raw[6]);
    std::printf("  sw_ver     [7-8]  = %u.%u\n", raw[7], raw[8]);
    std::printf("  reserved   [9]    = 0x%02X\n", raw[9]);
    std::printf("  crc        [10]   = 0x%02X\n", raw[10]);

    // 校验 config_t.crc：mdc::crc8(offset 4 起, sizeof-4 = 227B)
    const uint8_t calc = mdc::crc8(raw.data() + 4, raw.size() - 4);
    std::printf("  crc 校验          = %s (计算值 0x%02X)\n",
                calc == raw[10] ? "通过" : "失败", calc);

    // ── 5) mdc::parse_config 解析关键可写字段（含位域，无需手写偏移）──
    std::printf("== 关键字段 (mdc::parse_config) ==\n");
    mdc::Config cfg;
    if (mdc::parse_config(raw, cfg)) {
        std::printf("  baud_rate       = %u\n", cfg.baud_rate);
        std::printf("  cmd_timeout_ms  = %u ms\n", cfg.cmd_timeout_ms);
        std::printf("  protocol        = %u (1=SBUS 2=UART 3=ELRS)\n", cfg.protocol);
        std::printf("  ctrl_priority   = %u (0=USART2 优先 1=USB 优先)\n", cfg.ctrl_priority);
        std::printf("  control_mode    = ");
        for (int i = 0; i < 4; i++) std::printf("%d ", static_cast<int>(cfg.control_mode[i]));
        std::printf("(0=开环 1=速度 2=位置)\n");
        std::printf("  encoder_cpr     = ");
        for (int i = 0; i < 4; i++) std::printf("%u ", static_cast<unsigned>(cfg.encoder_cpr[i]));
        std::printf("\n");
    } else {
        std::printf("  parse_config 失败\n");
    }

    // ── 6) mdc::bin_motor_ctrl 打包示例（仅演示打包，不发送以免驱动电机）──
    std::printf("== bin_motor_ctrl 打包示例 (0x31) ==\n");
    const std::vector<uint8_t> ctrl = mdc::bin_motor_ctrl(100, -200, 0, 300);
    std::printf("  整帧 %zu 字节: ", ctrl.size());
    printHex(ctrl.data(), ctrl.size());
    std::printf("  DATA 段(16B): ");
    printHex(ctrl.data() + 3, 16);
    std::printf("  含义: 开环=PWM(±1000) / 速度=RPM / 位置=0.1°；int32 小端由库处理\n");

    sp.close();
    std::printf("== 完成，串口已关闭 ==\n");
    return 0;
}
