// ============================================================================
// 例程 05: status_monitor —— 状态订阅监控
//
// 流程: 打开串口 → SUBSCRIBE(0x40, 50ms) → 接收线程 readFrame 解析 0xF0
//       STATUS_REPORT（常规 56B / 扩展 72B 按 payload 长度自动兼容，全小端
//       memcpy 解析，避免结构体对齐问题）→ 控制台实时表格(\r 覆盖刷新)
//       → Ctrl+C 退出（先发送 UNSUBSCRIBE 0x41）
//
// 用法: status_monitor <串口>
//   Windows: status_monitor COM3
//   Linux:   status_monitor /dev/ttyUSB0
// ============================================================================

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "serial_port.h"

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
// 二进制帧工具（自包含实现）
// ============================================================================

// CRC8: 多项式 0x07，初值 0，计算范围 = CMD+LEN+DATA（不含 SYNC）
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

// 构建一帧: [0xAA][CMD][LEN][DATA...][CRC8]
static std::vector<uint8_t> buildFrame(uint8_t cmd, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> f;
    f.reserve(4 + data.size());
    f.push_back(0xAA);
    f.push_back(cmd);
    f.push_back(static_cast<uint8_t>(data.size()));
    f.insert(f.end(), data.begin(), data.end());
    f.push_back(crc8(&f[1], 2 + data.size()));
    return f;
}

// 读取一帧（滑动窗口找 0xAA → 按 LEN 收齐 → CRC8 校验）。
// rx 为跨调用保留的重组缓冲，订阅模式下帧是连续推流。
static bool readFrame(SerialPort& sp, std::vector<uint8_t>& rx, int timeoutMs,
                      uint8_t& cmd, std::vector<uint8_t>& payload) {
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(timeoutMs);

    while (std::chrono::steady_clock::now() < deadline) {
        for (;;) {                                  // 滑动窗口找完整帧
            auto it = std::find(rx.begin(), rx.end(), 0xAA);
            if (it == rx.end()) { rx.clear(); break; }
            if (it != rx.begin()) rx.erase(rx.begin(), it);

            if (rx.size() < 3) break;               // 帧头未齐
            const size_t len  = rx[2];
            const size_t need = 3 + len + 1;        // SYNC+CMD+LEN+DATA+CRC
            if (rx.size() < need) break;            // 数据未收齐

            if (crc8(&rx[1], 2 + len) == rx[need - 1]) {   // CRC 通过
                cmd = rx[1];
                payload.assign(rx.begin() + 3, rx.begin() + 3 + len);
                rx.erase(rx.begin(), rx.begin() + need);
                return true;
            }
            rx.erase(rx.begin());                   // CRC 失败：丢弃该 SYNC 继续找
        }

        const auto now = std::chrono::steady_clock::now();
        const int remain = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        if (remain <= 0) break;
        uint8_t tmp[256];
        const int n = sp.read(tmp, sizeof(tmp), remain < 200 ? remain : 200);
        if (n < 0) return false;
        if (n > 0) rx.insert(rx.end(), tmp, tmp + n);
    }
    return false;
}

// ============================================================================
// STATUS_REPORT(0xF0) 解析
//   常规 56B: [enc:16B][tgt:16B(float)][rpm:16B][sbus_frame_cnt:4B][sbus_ok_cnt:4B]
//   扩展 72B: 在 rpm 之后追加 [rpm_raw:16B]，再跟两个计数
// 全部小端；用 memcpy 到 int32_t/float，避免结构体对齐问题。
// ============================================================================
struct StatusData {
    int32_t  enc[4]      = {0, 0, 0, 0};
    float    tgt[4]      = {0, 0, 0, 0};
    int32_t  rpm[4]      = {0, 0, 0, 0};
    int32_t  rpm_raw[4]  = {0, 0, 0, 0};   // 仅扩展模式有效
    uint32_t sbus_frame_cnt = 0;
    uint32_t sbus_ok_cnt    = 0;
    bool     extended    = false;          // true = 72B 扩展帧
    uint64_t frames      = 0;              // 已解析帧数
};

static bool parseStatus(const uint8_t* p, size_t len, StatusData& out) {
    if (len == 56)      out.extended = false;
    else if (len == 72) out.extended = true;
    else return false;                      // 其他长度视为无效帧

    size_t off = 0;
    std::memcpy(out.enc,     p + off, 16); off += 16;   // enc[4]  int32
    std::memcpy(out.tgt,     p + off, 16); off += 16;   // tgt[4]  float
    std::memcpy(out.rpm,     p + off, 16); off += 16;   // rpm[4]  int32
    if (out.extended) {
        std::memcpy(out.rpm_raw, p + off, 16); off += 16; // rpm_raw[4] int32
    }
    std::memcpy(&out.sbus_frame_cnt, p + off, 4); off += 4;
    std::memcpy(&out.sbus_ok_cnt,    p + off, 4);
    out.frames++;
    return true;
}

// 接收线程：持续读帧并解析 0xF0，更新共享状态
static void receiveLoop(SerialPort& sp, std::mutex& mtx, StatusData& st,
                        std::atomic<bool>& running) {
    std::vector<uint8_t> rx;                // 帧重组缓冲
    uint8_t cmd = 0;
    std::vector<uint8_t> payload;
    while (running.load()) {
        if (!readFrame(sp, rx, 500, cmd, payload)) continue;
        if (cmd != 0xF0) continue;          // 忽略其他帧（如 ACK、DETECT_REPORT）
        StatusData tmp;
        if (parseStatus(payload.data(), payload.size(), tmp)) {
            tmp.frames = st.frames + 1;     // 帧计数跨锁维护
            std::lock_guard<std::mutex> lk(mtx);
            st = tmp;
        }
    }
}

// 控制台实时表格：\r + ANSI 光标上移覆盖刷新
static void renderTable(std::mutex& mtx, StatusData& st, int& lines) {
    StatusData snap;
    { std::lock_guard<std::mutex> lk(mtx); snap = st; }

    if (lines > 0) std::printf("\x1b[%dA", lines);      // 光标上移 N 行覆盖
    std::printf("\x1b[2K Motor Driver Controller — 状态监控 (订阅 50ms, Ctrl+C 退出)\n");
    std::printf("\x1b[2K CH |      enc      |      tgt      |      rpm");
    if (snap.extended) std::printf("   |    rpm_raw");
    std::printf("\n");
    std::printf("\x1b[2K----+---------------+---------------+--------------");
    if (snap.extended) std::printf("---+------------");
    std::printf("\n");
    for (int i = 0; i < 4; i++) {
        std::printf("\x1b[2K %2d | %12d | %12.1f | %12d",
                    i + 1, static_cast<int>(snap.enc[i]),
                    static_cast<double>(snap.tgt[i]), static_cast<int>(snap.rpm[i]));
        if (snap.extended)
            std::printf(" | %10d", static_cast<int>(snap.rpm_raw[i]));
        std::printf("\n");
    }
    std::printf("\x1b[2K 帧计数=%llu  SBUS 帧=%u 校验通过=%u  帧格式: %s\n",
                static_cast<unsigned long long>(snap.frames),
                snap.sbus_frame_cnt, snap.sbus_ok_cnt,
                snap.extended ? "扩展 72B" : "常规 56B");
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

    // 订阅 50ms 状态上报
    {
        const std::vector<uint8_t> d = {50 & 0xFF, (50 >> 8) & 0xFF};  // interval 2B LE
        const std::vector<uint8_t> f = buildFrame(0x40, d);
        sp.write(f.data(), f.size());
        std::printf("已发送 SUBSCRIBE (50ms)\n");
    }

    std::mutex mtx;
    StatusData st;
    std::atomic<bool> running{true};
    std::thread rx(receiveLoop, std::ref(sp), std::ref(mtx), std::ref(st), std::ref(running));

    // 渲染循环（主线程）
    int lines = 0;
    while (!g_ctrlC && running.load()) {
        renderTable(mtx, st, lines);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 退出：先停接收线程，再发送 UNSUBSCRIBE 并关闭
    running.store(false);
    rx.join();

    const std::vector<uint8_t> uf = buildFrame(0x41, {});   // UNSUBSCRIBE
    sp.write(uf.data(), uf.size());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    sp.close();
    std::printf("\n已发送 UNSUBSCRIBE 并关闭串口。\n");
    return 0;
}
