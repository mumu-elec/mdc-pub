// ============================================================================
// 例程 04: motor_control —— 实时电机控制
//
// - 启动时先发文本指令 "/priority 1"（USB 主控）并读回显确认
// - 发送线程按 interval-ms 周期连续发送 MOTOR_CTRL(0x31) 帧
//   （四通道 int32 小端目标值，含义取决于各通道模式：
//     开环=PWM ±1000 / 速度=RPM / 位置=0.1°）
// - 目标值斜坡平滑：当前值每周期向目标逼近最多 ramp 步进
// - 非阻塞键盘（Windows _kbhit / Linux select）：
//     '+' 目标 +=100   '-' 目标 -=100   'q' 退出（退出前发送全零安全停转）
//
// 用法:
//   motor_control --port COM3 --mode open --target 300 --interval-ms 50 --ramp 50
//
// 注意: 速度/位置闭环模式需要先配置好编码器 CPR 与 PID 参数
//       （/cpr、/speedctrl、/posctrl 或直接写 config_t），否则电机不会按预期运行。
// ============================================================================

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "serial_port.h"

#ifdef _WIN32
#include <conio.h>          // _kbhit / _getch
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

// ============================================================================
// 非阻塞 stdin（Windows: _kbhit / Linux: select + raw 模式）
// ============================================================================
#ifdef _WIN32
static bool stdinReady() { return _kbhit() != 0; }
static char stdinGetChar() { return static_cast<char>(_getch()); }
#else
static struct termios g_oldTio;
static void setStdinRaw(bool raw) {
    if (!isatty(STDIN_FILENO)) return;              // 非终端（重定向）时跳过
    if (raw) {
        struct termios tio;
        tcgetattr(STDIN_FILENO, &tio);
        g_oldTio = tio;
        tio.c_lflag &= ~(ICANON | ECHO);            // 关行缓冲与回显，按键立即生效
        tio.c_cc[VMIN]  = 1;
        tio.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &tio);
    } else {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_oldTio); // 恢复原终端设置
    }
}
static bool stdinReady() {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    struct timeval tv = {0, 0};
    return select(STDIN_FILENO + 1, &rfds, nullptr, nullptr, &tv) > 0;
}
static char stdinGetChar() {
    char c = 0;
    const ssize_t n = ::read(STDIN_FILENO, &c, 1);
    return n > 0 ? c : 0;
}
#endif

// ============================================================================
// 二进制帧工具（本例程只用到 0x31，直接在 main.cpp 内实现，保持自包含）
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

// 构建 MOTOR_CTRL(0x31) 帧: [0xAA][0x31][0x10][m1~m4:16B LE][CRC8]
static std::vector<uint8_t> buildCtrlFrame(const int32_t v[4]) {
    std::vector<uint8_t> data(16);
    for (int i = 0; i < 4; i++) {
        const uint32_t u = static_cast<uint32_t>(v[i]);     // 补码位模式
        data[i * 4 + 0] = static_cast<uint8_t>(u & 0xFF);
        data[i * 4 + 1] = static_cast<uint8_t>((u >> 8) & 0xFF);
        data[i * 4 + 2] = static_cast<uint8_t>((u >> 16) & 0xFF);
        data[i * 4 + 3] = static_cast<uint8_t>((u >> 24) & 0xFF);
    }
    std::vector<uint8_t> f;
    f.reserve(4 + data.size());
    f.push_back(0xAA);
    f.push_back(0x31);
    f.push_back(static_cast<uint8_t>(data.size()));
    f.insert(f.end(), data.begin(), data.end());
    f.push_back(crc8(&f[1], 2 + data.size()));
    return f;
}

// ============================================================================
// 文本指令发送 + 回显读取（用于 /priority、/mode 确认）
// ============================================================================
static std::string sendText(SerialPort& sp, const std::string& raw, int timeoutMs = 1000) {
    std::string cmd = raw;
    if (cmd.empty() || cmd.back() != '\n') cmd += '\n';
    sp.write(cmd);

    std::string out;
    bool seenNewline = false;
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        uint8_t buf[64];
        const int n = sp.read(buf, sizeof(buf), 40);
        if (n > 0) {
            out.append(reinterpret_cast<const char*>(buf), static_cast<size_t>(n));
            if (std::memchr(buf, '\n', static_cast<size_t>(n))) seenNewline = true;
        } else if (n < 0) {
            break;
        }
        if (seenNewline) {
            // 已读到换行：再等一小段确认无更多数据
            uint8_t b2[64];
            const int m = sp.read(b2, sizeof(b2), 40);
            if (m > 0) out.append(reinterpret_cast<const char*>(b2), static_cast<size_t>(m));
            break;
        }
    }
    return out;
}

// ============================================================================
// 发送线程共享状态
// ============================================================================
struct ControlState {
    std::atomic<int32_t> target{0};     // 主线程写、发送线程读
    std::atomic<bool>    running{true}; // 退出标志
};

// 发送线程: 按 interval_ms 周期发 0x31 帧，当前值以 ramp 步进逼近目标；
// 退出前补发全零帧安全停转。
static void controlLoop(SerialPort& sp, ControlState& st, int intervalMs, int32_t ramp) {
    int32_t cur[4] = {0, 0, 0, 0};
    while (st.running.load()) {
        const int32_t tgt = st.target.load();
        for (int i = 0; i < 4; i++) {                       // 斜坡平滑
            const int32_t diff = tgt - cur[i];
            if (diff > ramp)        cur[i] += ramp;
            else if (diff < -ramp)  cur[i] -= ramp;
            else                    cur[i] = tgt;
        }

        const std::vector<uint8_t> frame = buildCtrlFrame(cur);
        sp.write(frame.data(), frame.size());

        // 分小段等待，便于及时响应退出
        for (int w = 0; w < intervalMs && st.running.load(); w += 10)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 退出前发送全零，安全停转（连发两次确保到达）
    const int32_t zero[4] = {0, 0, 0, 0};
    const std::vector<uint8_t> zf = buildCtrlFrame(zero);
    sp.write(zf.data(), zf.size());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    sp.write(zf.data(), zf.size());
}

static void printUsage(const char* prog) {
    std::printf("用法: %s --port <串口> [选项]\n", prog);
    std::printf("  --port <p>        串口: COM3 / /dev/ttyUSB0 (必填)\n");
    std::printf("  --mode <m>        控制模式: open|speed|pos (默认 open)\n");
    std::printf("  --target <n>      初始目标值 (默认 0)\n");
    std::printf("  --interval-ms <n> 发送周期 (默认 50)\n");
    std::printf("  --ramp <n>        每周期斜坡步进 (默认 50)\n");
    std::printf("按键: '+' 目标+100 | '-' 目标-100 | 'q' 退出(发送全零)\n");
}

int main(int argc, char* argv[]) {
    std::string port;
    std::string mode = "open";
    int32_t target = 0;
    int intervalMs = 50;
    int ramp = 50;

    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            return (i + 1 < argc) ? std::string(argv[++i]) : std::string();
        };
        if (a == "--port")        port = next();
        else if (a == "--mode")   mode = next();
        else if (a == "--target") target = std::stoi(next());
        else if (a == "--interval-ms") intervalMs = std::stoi(next());
        else if (a == "--ramp")   ramp = std::stoi(next());
        else if (a == "--help" || a == "-h") { printUsage(argv[0]); return 0; }
        else { std::printf("未知参数: %s\n", a.c_str()); printUsage(argv[0]); return 1; }
    }
    if (port.empty()) { printUsage(argv[0]); return 1; }
    if (mode != "open" && mode != "speed" && mode != "pos") {
        std::printf("[错误] --mode 仅支持 open|speed|pos\n");
        return 1;
    }
    if (intervalMs < 20) intervalMs = 20;       // 协议建议 30/50/100ms 实时控制周期

    SerialPort sp;
    std::string err;
    if (!sp.open(port, 2000000, &err)) {
        std::printf("[错误] 打开串口失败: %s\n", err.c_str());
        return 1;
    }
    std::printf("== 已打开 %s @ 2000000-8N1 ==\n", port.c_str());

    // 1) USB 主控优先级（控制帧仲裁关键，见协议规范 §1）
    std::string r = sendText(sp, "/priority 1");
    std::printf("/priority 1 -> %s", r.c_str());

    // 2) 设置四通道控制模式（带参数=写入，仅 RAM，需 /save 持久化）
    for (int ch = 1; ch <= 4; ch++) {
        const std::string cmd = "/mode " + std::to_string(ch) + " " + mode;
        const std::string rr = sendText(sp, cmd);
        std::printf("%s -> %s", cmd.c_str(), rr.c_str());
    }

    // 3) 启动发送线程
    ControlState st;
    st.target.store(target);
    std::thread sender(controlLoop, std::ref(sp), std::ref(st), intervalMs,
                       static_cast<int32_t>(ramp));

    std::printf("\n控制中: mode=%s target=%d interval=%dms ramp=%d\n",
                mode.c_str(), (int)target, intervalMs, ramp);
    std::printf("按键: '+' 目标+100 | '-' 目标-100 | 'q' 退出(电机归零)\n");

#ifndef _WIN32
    setStdinRaw(true);                          // Linux 下进入原始键盘模式
#endif

    while (st.running.load()) {
        if (stdinReady()) {
            const char c = stdinGetChar();
            if (c == '+') {
                target += 100;
                st.target.store(target);
                std::printf("\r[+] target=%d   \n", (int)target);
            } else if (c == '-') {
                target -= 100;
                st.target.store(target);
                std::printf("\r[-] target=%d   \n", (int)target);
            } else if (c == 'q' || c == 'Q') {
                st.running.store(false);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

#ifndef _WIN32
    setStdinRaw(false);                         // 恢复终端
#endif

    sender.join();                              // 等发送线程补发全零后结束
    sp.close();
    std::printf("\n已退出，电机已归零，串口已关闭。\n");
    return 0;
}
