// ============================================================================
// 例程 05: status_monitor —— 状态订阅监控（基于 mdc_lib）
//
// 流程: 打开串口 → mdc::bin_subscribe(50) 订阅状态上报
//       → 接收线程用 mdc::Parser 流式解析（逐字节 feed，自动找 0xAA + CRC 校验）
//       → 收到 0xF0 STATUS_REPORT 时用 mdc::parse_status 解析
//         （56B 常规 / 72B 扩展按长度自动兼容）
//       → 控制台实时表格(\r 覆盖刷新)
//       → Ctrl+C 退出（先发送 mdc::bin_unsubscribe()）
//
// 用法: status_monitor <串口>
//   Windows: status_monitor COM3
//   Linux:   status_monitor /dev/ttyUSB0
// ============================================================================

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "mdc_lib.hpp"      // mdc_lib：bin_subscribe / Parser / parse_status / bin_unsubscribe
#include "serial_port.h"    // 串口收发（用户实现）

#ifdef _WIN32
#include <windows.h>
static volatile LONG g_ctrlC = 0;
static BOOL WINAPI ctrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        InterlockedExchange(&g_ctrlC, 1);
        return TRUE;                // 阻止默认终止，交给主线程清理（发退订）
    }
    return FALSE;
}
#else
#include <csignal>
static volatile std::sig_atomic_t g_ctrlC = 0;
static void sigHandler(int) { g_ctrlC = 1; }
#endif

// ============================================================================
// 监控共享状态：mdc::Status（mdc::parse_status 解析结果）+ 帧计数
// ============================================================================
struct MonitorState {
    mdc::Status st;         // 最近一帧解析结果（enc/tgt/rpm/rpm_raw/SBUS 计数）
    uint64_t    frames = 0; // 已解析帧数
};

// 接收线程：串口字节逐字节喂给 mdc::Parser（自动找 0xAA 同步字 + CRC 校验，
// 文本噪声自动丢弃），收到 0xF0 STATUS_REPORT 用 mdc::parse_status 解析
// （56B/72B 自动兼容），结果写入共享状态。
static void receiveLoop(SerialPort& sp, std::mutex& mtx, MonitorState& ms,
                        std::atomic<bool>& running) {
    mdc::Parser parser;
    while (running.load()) {
        uint8_t buf[256];
        const int n = sp.read(buf, sizeof(buf), 100);
        if (n < 0) break;                                   // 串口出错
        for (int i = 0; i < n; ++i) {
            auto frame = parser.feed(buf[i]);               // 逐字节喂给流式解析器
            if (!frame) continue;                           // 未收齐/校验未过
            const uint8_t cmd = frame->first;
            const auto& payload = frame->second;
            if (cmd != mdc::MD_CMD_STATUS_REPORT) continue; // 忽略其他帧（如 ACK）

            mdc::Status s;
            if (!mdc::parse_status(payload, s)) continue;   // 长度非法（非 56/72B）
            std::lock_guard<std::mutex> lk(mtx);
            ms.st = s;
            ms.frames++;
        }
    }
}

// 控制台实时表格：\r + ANSI 光标上移覆盖刷新
static void renderTable(std::mutex& mtx, MonitorState& ms, int& lines) {
    MonitorState snap;
    { std::lock_guard<std::mutex> lk(mtx); snap = ms; }
    const mdc::Status& st = snap.st;

    if (lines > 0) std::printf("\x1b[%dA", lines);      // 光标上移 N 行覆盖
    std::printf("\x1b[2K Motor Driver Controller — 状态监控 (订阅 50ms, Ctrl+C 退出)\n");
    std::printf("\x1b[2K CH |      enc      |      tgt      |      rpm");
    if (st.extended) std::printf("   |    rpm_raw");
    std::printf("\n");
    std::printf("\x1b[2K----+---------------+---------------+--------------");
    if (st.extended) std::printf("---+------------");
    std::printf("\n");
    for (int i = 0; i < 4; i++) {
        std::printf("\x1b[2K %2d | %12d | %12.1f | %12d",
                    i + 1, static_cast<int>(st.enc[i]),
                    static_cast<double>(st.tgt[i]), static_cast<int>(st.rpm[i]));
        if (st.extended)
            std::printf(" | %10d", static_cast<int>(st.rpm_raw[i]));
        std::printf("\n");
    }
    std::printf("\x1b[2K 帧计数=%llu  SBUS 帧=%u 校验通过=%u  帧格式: %s\n",
                static_cast<unsigned long long>(snap.frames),
                st.sbus_frame_cnt, st.sbus_ok_cnt,
                st.extended ? "扩展 72B" : "常规 56B");
    std::fflush(stdout);
    lines = 8;                              // 标题+表头+分隔+4行+底行 = 8 行
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

#ifdef _WIN32
    // 启用 Windows 控制台 ANSI VT 支持（表格覆盖刷新需要）
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD consoleMode = 0;
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &consoleMode))
        SetConsoleMode(hOut, consoleMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleCtrlHandler(ctrlHandler, TRUE);
#else
    std::signal(SIGINT, sigHandler);
#endif

    // 订阅 50ms 状态上报（mdc::bin_subscribe 打包）
    {
        const std::vector<uint8_t> f = mdc::bin_subscribe(50);
        sp.write(f.data(), f.size());
        std::printf("已发送 SUBSCRIBE (50ms)\n");
    }

    std::mutex mtx;
    MonitorState ms;
    std::atomic<bool> running{true};
    std::thread rx(receiveLoop, std::ref(sp), std::ref(mtx), std::ref(ms), std::ref(running));

    // 渲染循环（主线程）
    int lines = 0;
    while (!g_ctrlC && running.load()) {
        renderTable(mtx, ms, lines);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 退出：先停接收线程，再发送 UNSUBSCRIBE 并关闭
    running.store(false);
    rx.join();

    const std::vector<uint8_t> uf = mdc::bin_unsubscribe();
    sp.write(uf.data(), uf.size());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    sp.close();
    std::printf("\n已发送 UNSUBSCRIBE 并关闭串口。\n");
    return 0;
}
