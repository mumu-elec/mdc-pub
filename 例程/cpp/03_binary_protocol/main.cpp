// ============================================================================
// 例程 03: binary_protocol —— 二进制帧协议演示
//
// 流程: 打开串口 → PING(0x01) 打印 ACK 结果
//       → READ_PARAM(0x10) 读取 config_t(231B)
//       → 打印前 16 字节 HEX + 解析头部字段(offset 0~10) 并校验 crc
//
// 用法: binary_protocol <串口>
//   Windows: binary_protocol COM3
//   Linux:   binary_protocol /dev/ttyUSB0
// ============================================================================

#include <cstdio>
#include <string>
#include <vector>

#include "motor_driver.hpp"

static void printHex(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) std::printf("%02X ", p[i]);
    std::printf("\n");
}

// 小端读取辅助（协议规定多字节字段一律 LE）
static uint16_t le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
static uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
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

    MotorDriver md(sp);

    // ── 1) PING ──
    std::printf("== PING (0x01) ==\n");
    if (md.ping(500)) {
        std::printf("  OK: 设备在线，ACK err=0x00\n");
    } else {
        std::printf("  失败: 无 ACK 应答或 err=0xFF\n");
    }

    // ── 2) READ_PARAM ──
    std::printf("== READ_PARAM (0x10) ==\n");
    std::vector<uint8_t> cfg;
    if (!md.readParam(cfg, 1000)) {
        std::printf("  读取失败（无应答或 LEN != 231）\n");
        sp.close();
        return 1;
    }
    std::printf("  收到 %zu 字节 (期望 231)\n", cfg.size());

    // ── 3) 打印前 16 字节 HEX ──
    std::printf("  前 16 字节 HEX: ");
    printHex(cfg.data(), cfg.size() < 16 ? cfg.size() : 16);

    // ── 4) 解析头部 offset 0~10（受保护区域）──
    // 布局 v2.1: [0]magic u32 | [4]hw_ver_major u8 | [5]hw_ver_minor u8
    //           | [6]hw_variant u8 | [7]sw_ver_major u8 | [8]sw_ver_patch u8
    //           | [9]_reserved u8 | [10]crc u8（覆盖 offset 4 起 sizeof-4 字节）
    std::printf("== 头部字段解析 (offset 0~10) ==\n");
    const uint32_t magic = le32(&cfg[0]);
    std::printf("  magic      [0-3]  = 0x%08X %s\n", magic,
                magic == 0x4D445200u ? "(正确)" : "(异常)");
    std::printf("  hw_ver     [4-6]  = %u.%u.%u\n", cfg[4], cfg[5], cfg[6]);
    std::printf("  sw_ver     [7-8]  = %u.%u\n", cfg[7], cfg[8]);
    std::printf("  reserved   [9]    = 0x%02X\n", cfg[9]);
    std::printf("  crc        [10]   = 0x%02X\n", cfg[10]);

    // 校验 config_t.crc: crc8(offset 4 起, sizeof-4 = 227B)
    const uint8_t calc = MotorDriver::crc8(&cfg[4], cfg.size() - 4);
    std::printf("  crc 校验          = %s (计算值 0x%02X)\n",
                calc == cfg[10] ? "通过" : "失败", calc);

    // ── 5) 顺带解析几个常用可写字段 ──
    std::printf("== 常用字段 (offset 11~17) ==\n");
    std::printf("  baud_rate    [11] = %u\n", le32(&cfg[11]));
    std::printf("  cmd_timeout  [15] = %u ms\n", le16(&cfg[15]));
    std::printf("  comm_flags   [17] = 0x%02X (bit5=ctrl_priority: %d)\n",
                cfg[17], (cfg[17] >> 5) & 0x01);

    sp.close();
    std::printf("== 完成，串口已关闭 ==\n");
    return 0;
}
