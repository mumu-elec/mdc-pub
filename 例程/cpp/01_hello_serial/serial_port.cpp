// ============================================================================
// serial_port.cpp — 跨平台串口实现（见 serial_port.h 说明）
// ============================================================================

#include "serial_port.h"

SerialPort::SerialPort() {
#ifdef _WIN32
    h_ = INVALID_HANDLE_VALUE;
#else
    fd_ = -1;
#endif
}

SerialPort::~SerialPort() { close(); }

bool SerialPort::open(const std::string& port, int baud, std::string* err) {
    close();
    err_.clear();

#ifdef _WIN32
    // Windows 下 COM10 及以上必须使用 "\\.\COM10" 形式才能打开
    std::string winPort = port;
    if (winPort.rfind("COM", 0) == 0 && winPort.rfind("\\\\.\\", 0) != 0)
        winPort = "\\\\.\\" + winPort;

    h_ = CreateFileA(winPort.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h_ == INVALID_HANDLE_VALUE) {
        err_ = "CreateFile 失败: " + port + "（错误码 " + std::to_string(GetLastError()) + "）";
        if (err) *err = err_;
        return false;
    }

    // ── DCB 参数：2000000-8N1 ──
    DCB dcb;
    std::memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h_, &dcb)) {
        err_ = "GetCommState 失败（错误码 " + std::to_string(GetLastError()) + "）";
        close();
        if (err) *err = err_;
        return false;
    }
    dcb.BaudRate      = static_cast<DWORD>(baud);   // 2000000
    dcb.ByteSize      = 8;
    dcb.Parity        = NOPARITY;                   // N
    dcb.StopBits      = ONESTOPBIT;                 // 1
    dcb.fBinary       = TRUE;
    dcb.fParity       = FALSE;
    dcb.fOutxCtsFlow  = FALSE;
    dcb.fOutxDsrFlow  = FALSE;
    dcb.fDtrControl   = DTR_CONTROL_ENABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fTXContinueOnXoff = TRUE;
    dcb.fOutX         = FALSE;
    dcb.fInX          = FALSE;
    dcb.fErrorChar    = FALSE;
    dcb.fNull         = FALSE;
    dcb.fRtsControl   = RTS_CONTROL_ENABLE;
    dcb.fAbortOnError = FALSE;
    if (!SetCommState(h_, &dcb)) {
        err_ = "SetCommState 失败: 波特率 " + std::to_string(baud) + "（错误码 "
               + std::to_string(GetLastError()) + "）";
        close();
        if (err) *err = err_;
        return false;
    }

    // ── 超时：read() 每次调用前会按需更新，此处给一个默认值 ──
    COMMTIMEOUTS to;
    to.ReadIntervalTimeout     = 0;
    to.ReadTotalTimeoutMultiplier = 0;
    to.ReadTotalTimeoutConstant = 100;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant = 1000;
    SetCommTimeouts(h_, &to);

    // 清空收发缓冲，避免残留脏数据
    PurgeComm(h_, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return true;

#else // ─────────────────── Linux termios ───────────────────

    fd_ = ::open(port.c_str(), O_RDWR | O_NOCTTY);
    if (fd_ < 0) {
        err_ = "open 失败: " + port + "（" + std::strerror(errno) + "）";
        if (err) *err = err_;
        return false;
    }

    struct termios tio;
    if (tcgetattr(fd_, &tio) != 0) {
        err_ = std::string("tcgetattr 失败: ") + std::strerror(errno);
        close();
        if (err) *err = err_;
        return false;
    }

    // 原始模式：禁用行缓冲/回显/信号字符/软件流控
    tio.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tio.c_oflag &= ~OPOST;
    tio.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tio.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS);
    tio.c_cflag |= (CLOCAL | CREAD | CS8);          // 8N1

    // 波特率映射（本板固定 2000000 → B2000000）
    speed_t speed;
    switch (baud) {
        case 1200:    speed = B1200;    break;
        case 2400:    speed = B2400;    break;
        case 4800:    speed = B4800;    break;
        case 9600:    speed = B9600;    break;
        case 19200:   speed = B19200;   break;
        case 38400:   speed = B38400;   break;
        case 57600:   speed = B57600;   break;
        case 115200:  speed = B115200;  break;
        case 230400:  speed = B230400;  break;
        case 460800:  speed = B460800;  break;
        case 921600:  speed = B921600;  break;
        case 2000000: speed = B2000000; break;
        default:      speed = B2000000; break;      // 本板固定 2000000
    }
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);

    // 读写均用 select() 实现超时，故 VMIN/VTIME 置 0
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd_, TCSANOW, &tio) != 0) {
        err_ = std::string("tcsetattr 失败: ") + std::strerror(errno);
        close();
        if (err) *err = err_;
        return false;
    }
    tcflush(fd_, TCIOFLUSH);                        // 清空收发缓冲
    return true;
#endif
}

int SerialPort::write(const uint8_t* data, size_t len) {
    if (!isOpen()) {
        err_ = "串口未打开";
        return -1;
    }
    if (len == 0) return 0;

#ifdef _WIN32
    DWORD written = 0;
    if (!WriteFile(h_, data, static_cast<DWORD>(len), &written, nullptr)) {
        err_ = "WriteFile 失败（错误码 " + std::to_string(GetLastError()) + "）";
        return -1;
    }
    return static_cast<int>(written);
#else
    ssize_t n = ::write(fd_, data, len);
    if (n < 0) {
        err_ = std::string("write 失败: ") + std::strerror(errno);
        return -1;
    }
    return static_cast<int>(n);
#endif
}

int SerialPort::write(const std::string& s) {
    return write(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

int SerialPort::read(uint8_t* buf, size_t max_len, int timeout_ms) {
    if (!isOpen()) {
        err_ = "串口未打开";
        return -1;
    }
    if (max_len == 0) return 0;

#ifdef _WIN32
    // 按本次调用的超时更新 COMMTIMEOUTS（总超时 = ReadTotalTimeoutConstant）
    COMMTIMEOUTS to;
    to.ReadIntervalTimeout       = 0;
    to.ReadTotalTimeoutMultiplier = 0;
    to.ReadTotalTimeoutConstant  = static_cast<DWORD>(timeout_ms);
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant = 1000;
    SetCommTimeouts(h_, &to);

    DWORD got = 0;
    if (!ReadFile(h_, buf, static_cast<DWORD>(max_len), &got, nullptr)) {
        err_ = "ReadFile 失败（错误码 " + std::to_string(GetLastError()) + "）";
        return -1;
    }
    return static_cast<int>(got);                   // 0 = 超时无数据
#else
    // select() 等待可读，超时返回 0
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd_, &rfds);
    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int sel = ::select(fd_ + 1, &rfds, nullptr, nullptr, &tv);
    if (sel < 0) {
        if (errno == EINTR) return 0;               // 信号打断，视为超时
        err_ = std::string("select 失败: ") + std::strerror(errno);
        return -1;
    }
    if (sel == 0) return 0;                         // 超时无数据

    ssize_t n = ::read(fd_, buf, max_len);
    if (n < 0) {
        if (errno == EINTR) return 0;
        err_ = std::string("read 失败: ") + std::strerror(errno);
        return -1;
    }
    return static_cast<int>(n);
#endif
}

void SerialPort::close() {
#ifdef _WIN32
    if (h_ != INVALID_HANDLE_VALUE) {
        CloseHandle(h_);
        h_ = INVALID_HANDLE_VALUE;
    }
#else
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
#endif
}

bool SerialPort::isOpen() const {
#ifdef _WIN32
    return h_ != INVALID_HANDLE_VALUE;
#else
    return fd_ >= 0;
#endif
}

std::string SerialPort::lastError() const { return err_; }
