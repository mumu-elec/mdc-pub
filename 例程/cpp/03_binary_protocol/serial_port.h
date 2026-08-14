#ifndef SERIAL_PORT_H
#define SERIAL_PORT_H

// ============================================================================
// serial_port.h — 跨平台串口封装（Windows Win32 API / Linux termios）
//
// 适用设备 : Motor Driver Controller（STM32F401 + TB6612 四路直流电机驱动器）
// 通信参数 : USB 虚拟串口（CH340N）固定 2000000-8N1
// 依赖     : 零第三方库，仅系统 API
// 平台分支 :
//   _WIN32  → CreateFile / SetCommState(DCB, BaudRate=2000000) / COMMTIMEOUTS
//             / ReadFile / WriteFile
//   else    → termios（cfsetispeed / cfsetospeed B2000000 / tcsetattr）
//             + select() 超时读
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

// SerialPort: RAII 串口对象（析构自动关闭）。
//
// 用法:
//   SerialPort sp;
//   if (!sp.open("COM3")) { ... }              // 默认 2000000-8N1
//   sp.write("/version\n");
//   uint8_t buf[256];
//   int n = sp.read(buf, sizeof(buf), 1000);   // 最多阻塞 1000ms
class SerialPort {
public:
    SerialPort();
    ~SerialPort();                                  // 析构自动 close（RAII）
    SerialPort(const SerialPort&) = delete;         // 禁止拷贝
    SerialPort& operator=(const SerialPort&) = delete;

    // 打开串口。port: Windows "COM3" / Linux "/dev/ttyUSB0"。
    // baud 默认 2000000（本板固定值）。失败返回 false，错误信息写入 err（可为 nullptr）。
    bool open(const std::string& port, int baud = 2000000, std::string* err = nullptr);

    // 发送原始字节；返回实际写入字节数，失败返回 -1。
    int write(const uint8_t* data, size_t len);
    int write(const std::string& s);                // 便捷重载（文本指令）

    // 读取：最多读 max_len 字节，最多阻塞 timeout_ms 毫秒。
    // 返回实际读取字节数（0 = 超时无数据，-1 = 出错）。
    // 读不满 max_len 是正常现象（串口是流式设备）。
    int read(uint8_t* buf, size_t max_len, int timeout_ms);

    void close();                                   // 关闭端口（幂等）
    bool isOpen() const;
    std::string lastError() const;                  // 最近一次错误信息

private:
#ifdef _WIN32
    HANDLE h_;                                      // 串口句柄
#else
    int fd_;                                        // 文件描述符
#endif
    std::string err_;
};

#endif // SERIAL_PORT_H
