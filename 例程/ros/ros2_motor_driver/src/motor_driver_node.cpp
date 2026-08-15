// =====================================================================
// motor_driver_node.cpp — ROS2 (Humble) 驱动节点
//
// 硬件: Motor Driver Controller (STM32F401 + TB6612, 四路带编码器直流电机)
// 协议: 例程/common/协议规范.md (布局 v2.1, config_t = 231B)
//
// 本节点基于 mdc_lib 通用调用库 (mdc_lib/cpp/mdc_lib.hpp, 命名空间 mdc):
//   - 协议打包与解析全部由 mdc_lib 完成:
//       文本指令  -> mdc::text_build
//       二进制帧  -> mdc::bin_xxx (含 SYNC/CRC8 组帧)
//       收到数据  -> mdc::Parser 流式解析 (自动找 0xAA + CRC8 校验)
//       状态/配置 -> mdc::parse_status / mdc::parse_config / mdc::pack_config
//   - 节点不再自行实现 CRC8 / 组帧 / 滑动窗口解析, 只负责串口收发与 ROS 接口。
//
// 串口: USB 虚拟串口 (CH340N), 固定 2000000-8N1, Linux termios 直接操作,
//       不依赖第三方串口库; 读操作使用 select 超时。
// =====================================================================

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <cerrno>
#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

// mdc_lib 通用调用库 (header-only, 零依赖): include 路径见 CMakeLists.txt
#include "mdc_lib.hpp"

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/header.hpp"

#include "ros2_motor_driver/msg/motor_cmd.hpp"
#include "ros2_motor_driver/msg/motor_status.hpp"
#include "ros2_motor_driver/srv/get_config.hpp"
#include "ros2_motor_driver/srv/set_config.hpp"
#include "ros2_motor_driver/srv/save_config.hpp"

using namespace std::chrono_literals;

// ---------------- 串口类 (Linux termios, 零第三方依赖) ----------------
class SerialPort
{
public:
    SerialPort() = default;
    ~SerialPort() { close(); }

    // 打开串口并配置为 8N1 原始模式
    bool open(const std::string& port, int baud)
    {
        fd_ = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ < 0) return false;
        int fl = fcntl(fd_, F_GETFL, 0);
        if (fl >= 0) fcntl(fd_, F_SETFL, fl & ~O_NONBLOCK);

        struct termios tio;
        memset(&tio, 0, sizeof(tio));
        if (tcgetattr(fd_, &tio) != 0) { close(); return false; }
        cfmakeraw(&tio);                // 原始模式: 8N1, 无回显, 无行缓冲/信号处理
        tio.c_cflag |= (CLOCAL | CREAD);
        tio.c_cflag &= ~CRTSCTS;        // 无硬件流控
        tio.c_cc[VMIN]  = 0;            // 配合 select 实现超时读
        tio.c_cc[VTIME] = 0;
        speed_t sp = baud_to_speed(baud);
        if (cfsetispeed(&tio, sp) != 0 || cfsetospeed(&tio, sp) != 0) {
            close(); return false;
        }
        if (tcsetattr(fd_, TCSANOW, &tio) != 0) { close(); return false; }
        tcflush(fd_, TCIOFLUSH);        // 清空收发缓冲, 丢弃上电残留数据
        return true;
    }

    void close()
    {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }

    bool is_open() const { return fd_ >= 0; }

    // 带超时读: 返回 >0 读取字节数 / 0 超时 / -1 错误
    ssize_t read(uint8_t* buf, size_t max_len, int timeout_ms)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd_, &rfds);
        struct timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        int r = select(fd_ + 1, &rfds, nullptr, nullptr, &tv);
        if (r <= 0) return r;           // 0=超时, -1=select 错误
        ssize_t n = ::read(fd_, buf, max_len);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
        return n;
    }

    // 循环写直到写完
    ssize_t write(const uint8_t* data, size_t len)
    {
        size_t off = 0;
        while (off < len) {
            ssize_t n = ::write(fd_, data + off, len - off);
            if (n < 0) {
                if (errno == EINTR) continue;
                return -1;
            }
            off += (size_t)n;
        }
        return (ssize_t)off;
    }

private:
    static speed_t baud_to_speed(int baud)
    {
        switch (baud) {
            case 9600:    return B9600;
            case 19200:   return B19200;
            case 38400:   return B38400;
            case 57600:   return B57600;
            case 115200:  return B115200;
            case 230400:  return B230400;
            case 460800:  return B460800;
            case 500000:  return B500000;
            case 921600:  return B921600;
            case 1000000: return B1000000;
            case 1152000: return B1152000;
            case 1500000: return B1500000;
            case 2000000: return B2000000;  // 设备 USB 口固定 2000000-8N1 (默认)
            case 2500000: return B2500000;
            case 3000000: return B3000000;
            case 3500000: return B3500000;
            case 4000000: return B4000000;
            default:      return B2000000;
        }
    }

    int fd_ = -1;
};

// ---------------- 驱动节点 ----------------
class MotorDriverNode : public rclcpp::Node
{
public:
    // 命令应答类型
    enum ReplyKind {
        REPLY_NONE   = 0,
        REPLY_ACK_OK,     // ACK err=0x00
        REPLY_ACK_FAIL,   // ACK err=0xFF
        REPLY_DATA,       // 数据应答 (如 0x10 → 231B config_t)
    };

    explicit MotorDriverNode()
        : Node("motor_driver_node")
    {
        // ---- 参数 ----
        port_ = declare_parameter<std::string>("port", "/dev/ttyUSB0");
        baud_ = declare_parameter<int>("baud", 2000000);
        status_interval_ms_ = declare_parameter<int>("status_interval_ms", 50);
        send_priority_cmd_ = declare_parameter<bool>("send_priority_cmd", true);
        if (status_interval_ms_ < 20) {
            RCLCPP_WARN(get_logger(), "status_interval_ms 小于设备上报下限 20ms, 已钳位为 20");
            status_interval_ms_ = 20;
        }

        // ---- 话题 / 服务 ----
        status_pub_ = create_publisher<ros2_motor_driver::msg::MotorStatus>("motor_status", 10);
        cmd_sub_ = create_subscription<ros2_motor_driver::msg::MotorCmd>(
            "motor_cmd", 10,
            [this](const ros2_motor_driver::msg::MotorCmd::SharedPtr msg) { motor_cmd_cb(msg); });

        get_config_srv_ = create_service<ros2_motor_driver::srv::GetConfig>(
            "get_config",
            [this](const std::shared_ptr<rmw_request_id_t> /*req_id*/,
                   const std::shared_ptr<ros2_motor_driver::srv::GetConfig::Request> /*req*/,
                   const std::shared_ptr<ros2_motor_driver::srv::GetConfig::Response> resp) {
                std::vector<uint8_t> out;
                // READ_PARAM (0x10): mdc_lib 打包空帧
                int r = send_frame_wait_reply(mdc::MD_CMD_READ_PARAM,
                                              mdc::bin_read_param(), 1500, &out);
                if (r == REPLY_DATA && out.size() == mdc::MD_CONFIG_SIZE) {
                    std::copy(out.begin(), out.end(), resp->config.begin());
                    mdc::Config cfg;                            // mdc_lib 解析 config_t
                    if (mdc::parse_config(out, cfg)) {
                        RCLCPP_INFO(get_logger(),
                                    "READ_PARAM(0x10) 成功: 读取 config_t 231B "
                                    "(baud=%u timeout=%u mode=[%u,%u,%u,%u])",
                                    (unsigned)cfg.baud_rate, (unsigned)cfg.cmd_timeout_ms,
                                    (unsigned)cfg.control_mode[0], (unsigned)cfg.control_mode[1],
                                    (unsigned)cfg.control_mode[2], (unsigned)cfg.control_mode[3]);
                    }
                } else {
                    RCLCPP_ERROR(get_logger(), "READ_PARAM(0x10) 失败/超时 (r=%d, len=%zu)",
                                 r, out.size());
                }
            });

        set_config_srv_ = create_service<ros2_motor_driver::srv::SetConfig>(
            "set_config",
            [this](const std::shared_ptr<rmw_request_id_t> /*req_id*/,
                   const std::shared_ptr<ros2_motor_driver::srv::SetConfig::Request> req,
                   const std::shared_ptr<ros2_motor_driver::srv::SetConfig::Response> resp) {
                std::vector<uint8_t> d(req->config.begin(), req->config.end());
                mdc::Config cfg;
                if (!mdc::parse_config(d, cfg)) {   // mdc_lib 校验 231B config_t
                    resp->success = false;
                    RCLCPP_ERROR(get_logger(),
                                 "set_config: config 长度非法 (%zu, 期望 231B)", d.size());
                    return;
                }
                // 往返无损重打包 (受保护区置 0, 设备写入时自动还原), 再交给 mdc_lib 打包 0x11 帧
                int r = send_frame_wait_reply(mdc::MD_CMD_WRITE_PARAM,
                                              mdc::bin_write_param(mdc::pack_config(cfg)),
                                              2000, nullptr);   // WRITE_PARAM (0x11)
                resp->success = (r == REPLY_ACK_OK);
                RCLCPP_INFO(get_logger(), "WRITE_PARAM(0x11) %s",
                            resp->success ? "成功 (仅 RAM, 需 save_config 服务持久化)" : "失败/超时");
            });

        save_config_srv_ = create_service<ros2_motor_driver::srv::SaveConfig>(
            "save_config",
            [this](const std::shared_ptr<rmw_request_id_t> /*req_id*/,
                   const std::shared_ptr<ros2_motor_driver::srv::SaveConfig::Request> /*req*/,
                   const std::shared_ptr<ros2_motor_driver::srv::SaveConfig::Response> resp) {
                // SAVE_EEPROM (0x20): mdc_lib 打包空帧
                int r = send_frame_wait_reply(mdc::MD_CMD_SAVE_EEPROM,
                                              mdc::bin_save(), 3000, nullptr);
                resp->success = (r == REPLY_ACK_OK);
                RCLCPP_INFO(get_logger(), "SAVE_EEPROM(0x20) %s (写入 EEPROM 约 190ms)",
                            resp->success ? "成功" : "失败/超时");
            });

        // ---- 串口 ----
        if (!serial_.open(port_, baud_)) {
            RCLCPP_FATAL(get_logger(), "无法打开串口 %s (baud=%d), 请检查设备节点与 dialout 权限",
                         port_.c_str(), baud_);
            ok_ = false;
            return;
        }
        RCLCPP_INFO(get_logger(), "串口 %s 已打开 (baud=%d, 8N1)", port_.c_str(), baud_);

        // ---- 后台接收线程 ----
        recv_thread_ = std::thread(&MotorDriverNode::receive_loop, this);

        // ---- 启动序列: /priority 1 → SUBSCRIBE ----
        startup();
    }

    ~MotorDriverNode() override { shutdown_driver(); }

    bool is_ok() const { return ok_; }

    // 关闭: UNSUBSCRIBE + 发全零控制帧, 停止线程, 关闭串口 (幂等)
    void shutdown_driver()
    {
        std::lock_guard<std::mutex> lk(shutdown_mtx_);
        if (shutdown_done_) return;
        shutdown_done_ = true;

        if (serial_.is_open()) {
            const std::vector<uint8_t> unsub = mdc::bin_unsubscribe();  // 0x41 (关闭状态上报)
            write_bytes(unsub.data(), unsub.size());
            std::this_thread::sleep_for(50ms);
            const std::vector<uint8_t> zero = mdc::bin_motor_ctrl(0, 0, 0, 0);  // 全零 0x31
            write_bytes(zero.data(), zero.size());
            RCLCPP_INFO(get_logger(), "已发送 UNSUBSCRIBE(0x41) 与全零 MOTOR_CTRL(0x31)");
        }

        stop_ = true;
        if (recv_thread_.joinable()) recv_thread_.join();
        serial_.close();
        RCLCPP_INFO(get_logger(), "驱动节点已关闭, 串口已释放");
    }

private:
    // ================= 启动序列 =================
    void startup()
    {
        // 1) 文本指令 /priority 1 (USB 主控), mdc_lib 构造文本行, 等待回显
        if (send_priority_cmd_) {
            {
                std::lock_guard<std::mutex> lk(text_mtx_);
                text_rx_.clear();
            }
            const std::string text = mdc::text_build("/priority", "1");  // "/priority 1\n"
            write_bytes((const uint8_t*)text.data(), text.size());
            RCLCPP_INFO(get_logger(), "已发送文本指令: %s (等待回显...)", text.c_str());

            std::unique_lock<std::mutex> lk(text_mtx_);
            bool echoed = text_cv_.wait_for(lk, 2s, [this] {
                return text_rx_.find('\n') != std::string::npos;
            });
            std::string echo = text_rx_;
            lk.unlock();
            if (echoed) {
                size_t pos = echo.find('\n');
                std::string line = (pos == std::string::npos) ? echo : echo.substr(0, pos);
                RCLCPP_INFO(get_logger(), "/priority 1 回显: \"%s\" (USB 主控已就绪)", line.c_str());
            } else {
                RCLCPP_WARN(get_logger(),
                            "2s 内未收到 /priority 1 回显, 请检查设备/接线; 已收到: \"%s\"",
                            echo.c_str());
            }
        } else {
            RCLCPP_WARN(get_logger(),
                        "send_priority_cmd=false, 跳过 /priority 1; "
                        "若 USB 非主控, 控制帧 0x31 将被设备仲裁拒绝");
        }

        // 2) SUBSCRIBE (0x40): mdc_lib 打包 [interval_ms:2B LE]
        int r = send_frame_wait_reply(mdc::MD_CMD_SUBSCRIBE,
                                      mdc::bin_subscribe((uint16_t)status_interval_ms_),
                                      1000, nullptr);
        if (r == REPLY_ACK_OK) {
            RCLCPP_INFO(get_logger(), "SUBSCRIBE(0x40) 成功: 状态上报间隔 %d ms",
                        status_interval_ms_);
        } else {
            RCLCPP_ERROR(get_logger(), "SUBSCRIBE(0x40) 失败/超时 (r=%d), 将收不到 /motor_status",
                         r);
        }
    }

    // ================= 后台接收线程 =================
    void receive_loop()
    {
        uint8_t buf[512];
        while (!stop_) {
            if (!serial_.is_open()) break;
            ssize_t n = serial_.read(buf, sizeof(buf), 100);  // select 100ms 超时
            if (n > 0) {
                on_rx(buf, (size_t)n);
            } else if (n < 0) {
                RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 2000, "串口读错误");
            }
        }
        RCLCPP_INFO(get_logger(), "接收线程退出");
    }

    // 收到原始字节: ① 存入文本回显缓冲 ② 交给 mdc::Parser 流式解析
    void on_rx(const uint8_t* d, size_t n)
    {
        {   // 文本回显缓冲 (供 /priority 1 回显等待, 仅启动阶段使用)
            std::lock_guard<std::mutex> lk(text_mtx_);
            text_rx_.append((const char*)d, n);
            if (text_rx_.size() > 4096) text_rx_.erase(0, text_rx_.size() - 4096);
        }
        text_cv_.notify_all();

        // mdc_lib 流式解析器: 自动找 0xAA 同步 + CRC8 校验, 文本噪声字节自动丢弃
        auto frames = parser_.feed(d, n);
        for (auto& f : frames) {
            handle_frame(f.first, f.second);
        }
    }

    // ================= 帧分发 (mdc::Parser 输出完整帧后) =================
    void handle_frame(uint8_t cmd, const std::vector<uint8_t>& payload)
    {
        if (cmd == mdc::MD_CMD_STATUS_REPORT) {               // 0xF0: 设备主动推送
            handle_status(payload);
        } else if (cmd == mdc::MD_CMD_DETECT_REPORT ||
                   cmd == mdc::MD_CMD_SBUS_DATA) {            // 0xF1/0xF2: 未启用, 忽略
            RCLCPP_DEBUG(get_logger(), "忽略主动上报帧 CMD=0x%02X LEN=%zu", cmd, payload.size());
        } else if (payload.size() == 1) {                     // ACK 帧: DATA = [err]
            mdc::Ack ack;
            if (mdc::parse_ack(payload, ack)) handle_ack(cmd, ack.err);
        } else if (cmd == mdc::MD_CMD_READ_PARAM &&
                   payload.size() == mdc::MD_CONFIG_SIZE) {   // READ_PARAM 应答: config_t 231B
            handle_data_reply(payload);
        } else {
            RCLCPP_WARN(get_logger(), "未识别帧 CMD=0x%02X LEN=%zu", cmd, payload.size());
        }
    }

    // 0xF0 STATUS_REPORT: mdc_lib 解析 (常规 56B / 扩展 72B 自动兼容)
    void handle_status(const std::vector<uint8_t>& payload)
    {
        mdc::Status st;
        if (!mdc::parse_status(payload, st)) {
            RCLCPP_WARN(get_logger(), "STATUS_REPORT 解析失败 (len=%zu, 期望 56/72)",
                        payload.size());
            return;
        }
        ros2_motor_driver::msg::MotorStatus msg;
        msg.header.stamp = now();
        msg.header.frame_id = "motor_driver";
        for (int i = 0; i < 4; i++) {
            msg.enc[i] = st.enc[i];          // 编码器累计脉冲
            msg.tgt[i] = st.tgt[i];          // 当前目标值 (float)
            msg.rpm[i] = st.rpm[i];          // 滤波后转速
        }
        msg.sbus_frame_cnt = st.sbus_frame_cnt;
        msg.sbus_ok_cnt    = st.sbus_ok_cnt;
        status_pub_->publish(msg);
    }

    // ACK 帧: 完成正在等待的命令 (promise 语义, 条件变量通知)
    void handle_ack(uint8_t cmd, uint8_t err)
    {
        {
            std::lock_guard<std::mutex> lk(cmd_mtx_);
            if (cmd == pending_cmd_) {
                reply_kind_ = (err == mdc::MD_ERR_OK) ? REPLY_ACK_OK : REPLY_ACK_FAIL;
                reply_received_ = true;
            } else {
                RCLCPP_DEBUG(get_logger(), "收到未等待的 ACK: CMD=0x%02X err=0x%02X", cmd, err);
            }
        }
        ack_cv_.notify_all();
    }

    // 数据应答 (如 0x10 → 231B config_t)
    void handle_data_reply(const std::vector<uint8_t>& data)
    {
        {
            std::lock_guard<std::mutex> lk(cmd_mtx_);
            if (pending_cmd_ == mdc::MD_CMD_READ_PARAM) {
                reply_data_ = data;
                reply_kind_ = REPLY_DATA;
                reply_received_ = true;
            }
        }
        ack_cv_.notify_all();
    }

    // ================= 发送 =================
    bool write_bytes(const uint8_t* d, size_t n)
    {
        std::lock_guard<std::mutex> lk(write_mtx_);
        return serial_.write(d, n) == (ssize_t)n;
    }

    // 发送 mdc_lib 打包好的整帧并等待应答 (ACK 或数据帧),
    // 串口命令串行化 (一次一个在途命令)。
    // 返回: REPLY_ACK_OK / REPLY_ACK_FAIL / REPLY_DATA / -1 (超时或 IO 错误)
    int send_frame_wait_reply(uint8_t cmd, const std::vector<uint8_t>& frame,
                              int timeout_ms, std::vector<uint8_t>* out)
    {
        if (!serial_.is_open()) return -1;
        {
            std::lock_guard<std::mutex> lk(cmd_mtx_);
            pending_cmd_ = cmd;
            reply_received_ = false;
            reply_kind_ = REPLY_NONE;
            reply_data_.clear();
        }
        if (!write_bytes(frame.data(), frame.size())) {
            std::lock_guard<std::mutex> lk(cmd_mtx_);
            pending_cmd_ = 0;
            return -1;
        }
        std::unique_lock<std::mutex> lk(cmd_mtx_);
        bool got = ack_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                                    [this] { return reply_received_; });
        int kind = got ? reply_kind_ : -1;
        if (out && kind == REPLY_DATA) *out = reply_data_;
        pending_cmd_ = 0;
        return kind;
    }

    // ================= /motor_cmd 订阅回调 =================
    void motor_cmd_cb(const ros2_motor_driver::msg::MotorCmd::SharedPtr msg)
    {
        // 30Hz 限流: 高于 30Hz 的发布被跳过, 只发送最新值 (设备按 /timeout 超时保护)
        auto now_t = std::chrono::steady_clock::now();
        if (now_t - last_ctrl_send_ < 33ms) {
            if (!throttle_warned_) {
                RCLCPP_WARN(get_logger(),
                            "/motor_cmd 发布频率超过 30Hz 已限流, 建议 10~30Hz");
                throttle_warned_ = true;
            }
            return;
        }
        throttle_warned_ = false;
        last_ctrl_send_ = now_t;

        // mdc_lib 打包 0x31 MOTOR_CTRL [m1~m4: 4×int32 LE] (核心控制帧)
        const std::vector<uint8_t> frame =
            mdc::bin_motor_ctrl(msg->target[0], msg->target[1],
                                msg->target[2], msg->target[3]);
        if (!write_bytes(frame.data(), frame.size())) {
            RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 2000, "串口写 0x31 失败");
        }
    }

    // ================= 成员 =================
    // 参数
    std::string port_;
    int baud_ = 2000000;
    int status_interval_ms_ = 50;
    bool send_priority_cmd_ = true;
    bool ok_ = false;

    // ROS 句柄
    rclcpp::Publisher<ros2_motor_driver::msg::MotorStatus>::SharedPtr status_pub_;
    rclcpp::Subscription<ros2_motor_driver::msg::MotorCmd>::SharedPtr cmd_sub_;
    rclcpp::Service<ros2_motor_driver::srv::GetConfig>::SharedPtr get_config_srv_;
    rclcpp::Service<ros2_motor_driver::srv::SetConfig>::SharedPtr set_config_srv_;
    rclcpp::Service<ros2_motor_driver::srv::SaveConfig>::SharedPtr save_config_srv_;

    // 串口 / 线程
    SerialPort serial_;
    std::thread recv_thread_;
    std::atomic<bool> stop_{false};
    bool shutdown_done_ = false;
    std::mutex shutdown_mtx_;
    std::mutex write_mtx_;

    // 命令/应答状态 (一次一个在途命令)
    std::mutex cmd_mtx_;
    std::condition_variable ack_cv_;
    uint8_t pending_cmd_ = 0;
    bool reply_received_ = false;
    int reply_kind_ = REPLY_NONE;
    std::vector<uint8_t> reply_data_;

    // 文本回显缓冲 (启动阶段 /priority 1 回显等待)
    std::mutex text_mtx_;
    std::condition_variable text_cv_;
    std::string text_rx_;

    // mdc_lib 流式解析器 (仅在接收线程访问)
    mdc::Parser parser_;

    // 0x31 控制帧限流
    std::chrono::steady_clock::time_point last_ctrl_send_{};
    bool throttle_warned_ = false;
};

// ================= main =================
int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<MotorDriverNode>();
    if (!node->is_ok()) {
        RCLCPP_FATAL(rclcpp::get_logger("motor_driver_node"), "串口初始化失败, 节点退出");
        rclcpp::shutdown();
        return 1;
    }
    RCLCPP_INFO(node->get_logger(), "节点就绪, 开始处理回调 (Ctrl+C 退出)");
    rclcpp::spin(node);
    node->shutdown_driver();   // UNSUBSCRIBE + 全零控制帧
    rclcpp::shutdown();
    return 0;
}
