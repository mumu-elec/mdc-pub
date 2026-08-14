// ============================================================================
// 例程 01: hello_serial —— 最小连通测试
//
// 流程: 打开串口(2000000-8N1) → 发送 "/version\n" → 读取回显(≤2s) 打印
//       → 发送 "/status\n" → 读取回显打印 → 关闭串口
//
// 用法:
//   Windows: hello_serial COM3
//   Linux:   hello_serial /dev/ttyUSB0
//
// 依赖: serial_port.h / serial_port.cpp（本目录内自带，零第三方库）
// ============================================================================

#include <chrono>
#include <cstdio>
#include <string>

#include "serial_port.h"

// 读取回显：持续读取直到连续 idle_ms 毫秒无新数据，或超过 totalTimeout_ms。
// 用于兼容单行（如 /version）与多行（如 /status）两种回显。
static std::string readEcho(SerialPort& sp, int totalTimeoutMs = 2000, int idleMs = 150) {
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
        const int n = sp.read(buf, sizeof(buf), remainMs < idleMs ? remainMs : idleMs);
        if (n < 0) break;                                   // 读出错
        if (n > 0) {
            out.append(reinterpret_cast<const char*>(buf), static_cast<size_t>(n));
            lastData = std::chrono::steady_clock::now();
        }
    }
    return out;
}

static void printUsage(const char* prog) {
    std::printf("用法: %s <串口>\n", prog);
    std::printf("  Windows 示例: %s COM3\n", prog);
    std::printf("  Linux   示例: %s /dev/ttyUSB0\n", prog);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }
    const std::string port = argv[1];

    SerialPort sp;
    std::string err;
    if (!sp.open(port, 2000000, &err)) {
        std::printf("[错误] 打开串口失败: %s\n", err.c_str());
        return 1;
    }
    std::printf("== 已打开 %s @ 2000000-8N1 ==\n", port.c_str());

    // 1) 发送 /version，读取回显（最多 2 秒）
    const std::string cmdVersion = "/version\n";
    if (sp.write(cmdVersion) < 0) {
        std::printf("[错误] 发送失败: %s\n", sp.lastError().c_str());
        sp.close();
        return 1;
    }
    std::printf(">> %s", cmdVersion.c_str());
    std::printf("<< %s\n", readEcho(sp, 2000).c_str());

    // 2) 发送 /status，读取回显（多行输出，最多 2 秒）
    const std::string cmdStatus = "/status\n";
    if (sp.write(cmdStatus) < 0) {
        std::printf("[错误] 发送失败: %s\n", sp.lastError().c_str());
        sp.close();
        return 1;
    }
    std::printf(">> %s", cmdStatus.c_str());
    std::printf("<< %s\n", readEcho(sp, 2000).c_str());

    sp.close();
    std::printf("== 完成，串口已关闭 ==\n");
    return 0;
}
