// ============================================================================
// 例程 02: text_commands —— 文本指令交互终端（基于 mdc_lib）
//
// - 交互循环: 从 stdin 读一行 → 拆分为「指令 + 参数」→ mdc::text_build 打包
//   （含结尾 '\n'）→ 发送 → 打印回显
// - 数字快捷键菜单: 1=查版本  2=查实时状态  3=查全部配置
//                  4=通道1速度模式  5=通道1开环  6=保存EEPROM  7=退出
// - 也可直接输入任意指令（如 /mode 2 pos、/cpr 1 360、/priority 1）
//
// 用法: text_commands <串口>
//   Windows: text_commands COM3
//   Linux:   text_commands /dev/ttyUSB0
// ============================================================================

#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

#include "mdc_lib.hpp"      // mdc_lib：text_build / text_version / text_check / ...
#include "serial_port.h"    // 串口收发（用户实现）

static SerialPort g_sp;

// 读取回显：持续读取直到连续 idle_ms 毫秒无新数据，或超过 totalTimeoutMs。
static std::string readEcho(int totalTimeoutMs = 2000, int idleMs = 150) {
    std::string out;
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(totalTimeoutMs);
    auto lastData = std::chrono::steady_clock::now();

    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) break;
        if (!out.empty() && (now - lastData) >= std::chrono::milliseconds(idleMs)) break;

        const int remainMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        if (remainMs <= 0) break;

        uint8_t buf[128];
        const int n = g_sp.read(buf, sizeof(buf), remainMs < idleMs ? remainMs : idleMs);
        if (n < 0) break;
        if (n > 0) {
            out.append(reinterpret_cast<const char*>(buf), static_cast<size_t>(n));
            lastData = std::chrono::steady_clock::now();
        }
    }
    return out;
}

// 把用户输入行拆分为「指令 + 参数」，交给 mdc::text_build 打包（含结尾 '\n'）。
// 例: "/mode 2 pos"  → cmd="/mode" args="2 pos" → "/mode 2 pos\n"
//      "/version"    → 无参数 → "/version\n"
static std::string buildText(const std::string& line) {
    const size_t pos = line.find_first_of(" \t");
    if (pos == std::string::npos)
        return mdc::text_build(line, nullptr);          // 无参数 → "/cmd\n"

    const std::string cmd = line.substr(0, pos);
    const size_t b = line.find_first_not_of(" \t", pos);
    if (b == std::string::npos)
        return mdc::text_build(cmd, nullptr);           // 只有空白 → 按无参数处理
    const size_t e = line.find_last_not_of(" \t");
    return mdc::text_build(cmd, line.substr(b, e - b + 1));
}

// 发送一条由 mdc_lib 打包好的文本指令（含 '\n'）并读取、打印回显
static void sendAndEcho(const std::string& cmd) {
    if (g_sp.write(cmd) < 0) {
        std::printf("[错误] 发送失败: %s\n", g_sp.lastError().c_str());
        return;
    }
    std::printf(">> %s", cmd.c_str());
    std::printf("<< %s\n", readEcho().c_str());
}

static void printMenu() {
    std::printf("\n────── 快捷键菜单 ──────\n");
    std::printf(" 1 = 查版本        /version\n");
    std::printf(" 2 = 查实时状态    /check\n");
    std::printf(" 3 = 查全部配置    /status\n");
    std::printf(" 4 = 通道1速度模式 /mode 1 speed\n");
    std::printf(" 5 = 通道1开环     /mode 1 open\n");
    std::printf(" 6 = 保存到EEPROM  /save\n");
    std::printf(" 7 = 退出\n");
    std::printf(" 或直接输入任意指令（如 /mode 2 pos、/cpr 1 360、/priority 1、/timeout 500）\n");
    std::printf(" 输入 menu 或 ? 重新显示本菜单\n");
    std::printf("─────────────────────\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::printf("用法: %s <串口>\n", argv[0]);
        std::printf("  Windows 示例: %s COM3\n", argv[0]);
        std::printf("  Linux   示例: %s /dev/ttyUSB0\n", argv[0]);
        return 1;
    }

    std::string err;
    if (!g_sp.open(argv[1], 2000000, &err)) {
        std::printf("[错误] 打开串口失败: %s\n", err.c_str());
        return 1;
    }
    std::printf("== 已打开 %s @ 2000000-8N1，输入 /help 查看全部指令 ==\n", argv[1]);
    printMenu();

    std::string line;
    while (true) {
        std::printf("> ");                                  // 提示符
        std::fflush(stdout);

        if (!std::getline(std::cin, line)) break;           // EOF: Ctrl+Z(Win) / Ctrl+D(Linux)

        if (line == "7" || line == "exit" || line == "quit") break;
        if (line == "menu" || line == "?") { printMenu(); continue; }
        if (line == "1")        sendAndEcho(mdc::text_version());
        else if (line == "2")   sendAndEcho(mdc::text_check());
        else if (line == "3")   sendAndEcho(mdc::text_status());
        else if (line == "4")   sendAndEcho(mdc::text_mode(1, "speed"));
        else if (line == "5")   sendAndEcho(mdc::text_mode(1, "open"));
        else if (line == "6")   sendAndEcho(mdc::text_save());
        else                    sendAndEcho(buildText(line));   // 任意指令：拆分后打包
    }

    g_sp.close();
    std::printf("== 已退出，串口已关闭 ==\n");
    return 0;
}
