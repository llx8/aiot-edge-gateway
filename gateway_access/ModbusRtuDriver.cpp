#include "ModbusRtuDriver.h"
#include <chrono>
#include <fcntl.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <sys/select.h>
#include "Logger.h"
#include "ModbusRtu.h"
#include "InternalMessage.h"
#include <sys/eventfd.h>


// 构造函数
ModbusRtuDriver::ModbusRtuDriver(const std::string& serial_port, uint8_t slave_addr, uint16_t poll_interval_ms, uint16_t reg_start, uint16_t reg_count)
    : serial_port_(serial_port)
    , slave_addr_(slave_addr)
    , poll_interval_ms_(poll_interval_ms)
    , reg_start_(reg_start)
    , reg_count_(reg_count)
{
    running_.store(false);
}

// 析构函数
ModbusRtuDriver::~ModbusRtuDriver()
{
    stop();
}

// 打开串口文件
bool ModbusRtuDriver::open_serial() {
    // 打开文件
    int fd = open(serial_port_.c_str(), O_RDWR | O_NOCTTY);
    if (fd < 0) {
        GetLogger("ModbusRtuDriver")->error("Failed to open serial port: {}", serial_port_);    
        return false;
    }
    struct termios options{};
    if (tcgetattr(fd, &options) < 0) {
        GetLogger("ModbusRtuDriver")->error("tcgetattr failed: {}", strerror(errno));
        close(fd);
        return false;
    }

    cfsetispeed(&options, B9600);
    cfsetospeed(&options, B9600);
    options.c_cflag |= CS8; // 8位数据位
    options.c_cflag &= ~PARENB; // 无校验位
    options.c_cflag &= ~CSTOPB; // 1位停止位
    options.c_cflag |= CLOCAL | CREAD; // 本地模式，允许读取

    options.c_cc[VMIN] = 0; // 非阻塞读取，立即返回
    options.c_cc[VTIME] = 5; // 超时时间，单位为秒
    if (tcsetattr(fd, TCSANOW, &options) < 0) {
        GetLogger("ModbusRtuDriver")->error("tcsetattr failed: {}", strerror(errno));
        close(fd);
        return false;
    }
    serial_fd_ = fd;
    return true;
}

// write (event_fd)
void ModbusRtuDriver::notify_main_thread(){
    uint64_t cnt = 1;
    write(event_fd_, &cnt, sizeof(cnt));
}

// 启动轮询线程
void ModbusRtuDriver::start() {
    if (!open_serial()) {
        GetLogger("ModbusRtuDriver")->error("Failed to open serial port: {}", serial_port_);
        return;
    }
    event_fd_ = eventfd(0, EFD_NONBLOCK);
    if (event_fd_ < 0) {
        GetLogger("ModbusRtuDriver")->error("Failed to create eventfd: {}", strerror(errno));
        close(serial_fd_);
        serial_fd_ = -1;
        return;
    }
    running_.store(true);
    poll_thread_ = std::thread(&ModbusRtuDriver::poll_loop, this);
}

// 停止轮询线程
void ModbusRtuDriver::stop() {
    running_.store(false);
    if (poll_thread_.joinable()) {
        poll_thread_.join();
    }
    if (serial_fd_ != -1) {
        close(serial_fd_);
        serial_fd_ = -1;
    }
    if (event_fd_ != -1) {
        close(event_fd_);
        event_fd_ = -1;
    }
}

int ModbusRtuDriver::event_fd() const {
    return event_fd_;
}

std::string_view ModbusRtuDriver::name() const {
    return "Modbus-RTU";
}

void ModbusRtuDriver::set_poll_interval(uint16_t interval_ms) {
    poll_interval_ms_.store(interval_ms);
    GetLogger("ModbusRtuDriver")->info("poll interval changed to {}ms", interval_ms);
}

// 主循环，轮询串口数据并处理
void ModbusRtuDriver::poll_loop() {
    while (running_.load()) {
        // —— 超时剔除：已挂起则跳过正常轮询，等待复活间隔 ——
        if (suspended_) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - last_revival_attempt_).count();
            if (elapsed < kRevivalIntervalSec) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            // 尝试复活：重开串口
            GetLogger("ModbusRtuDriver")->info("尝试复活从站 0x{:02x} (挂起 {}s)",
                slave_addr_, kRevivalIntervalSec);
            if (!open_serial()) {
                last_revival_attempt_ = std::chrono::steady_clock::now();
                continue;
            }
            suspended_ = false;
            consecutive_timeouts_ = 0;
        }

        // 1 编码请求
        ModbusRequest req{slave_addr_, 0x03, reg_start_, reg_count_};
        auto frame = encode_request(req);
        // 2 写串口：先丢弃上次残留字节（上一轮解码失败/未读完时），避免污染新帧
        tcflush(serial_fd_, TCIOFLUSH);
        ssize_t written = write(serial_fd_, frame.data(), frame.size());
        if (written < 0) {
            GetLogger("ModbusRtuDriver")->error("serial write failed: {}", strerror(errno));
        }
        // 3 等待串口数据
        uint8_t buf[256];
        ssize_t n = read(serial_fd_, buf, sizeof(buf));
        if (n < 0) {
            GetLogger("ModbusRtuDriver")->error("serial read failed: {}", strerror(errno));
        }
        // 4 解码
        ModbusResponse resp;
        if (n > 0 && decode_response(buf, n, resp)) {
            // 成功：清零超时计数
            consecutive_timeouts_ = 0;
            // 处理响应 回调函数
            InternalMessage msg;
            msg.source_type = 0;  // 0 = Modbus
            msg.node_id = slave_addr_;
            msg.tlv_type = 0x01;
            if (resp.registers.size() > 0) {
                // 寄存器为 uint16，payload 为字节流，须按大端序列化；
                // 原先 assign 逐元素隐式转 uint8 会丢失每个寄存器值的高字节
                msg.payload.resize(resp.registers.size() * 2);
                for (size_t i = 0; i < resp.registers.size(); ++i) {
                    msg.payload[i * 2]     = static_cast<uint8_t>(resp.registers[i] >> 8);
                    msg.payload[i * 2 + 1] = static_cast<uint8_t>(resp.registers[i] & 0xFF);
                }
            }
            m_on_data(msg);
            // 5 通知主线程
            notify_main_thread();
        } else {
            // 超时或解码失败
            consecutive_timeouts_++;
            if (consecutive_timeouts_ >= kMaxTimeouts) {
                GetLogger("ModbusRtuDriver")->warn(
                    "从站 0x{:02x} 连续{}次超时，挂起({}s后尝试复活)",
                    slave_addr_, consecutive_timeouts_, kRevivalIntervalSec);
                suspended_ = true;
                last_revival_attempt_ = std::chrono::steady_clock::now();
                close(serial_fd_);
                serial_fd_ = -1;
            }
        }
        // 6 等下一轮
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms_.load()));
    }
}

#ifndef MODBUS_RTU_STATIC  // 静态链接时跳过(避免 dlopen 工厂函数冲突)
extern "C" ISensorDriver* create_driver() {
    return new ModbusRtuDriver("/dev/ttyUSB0", 1, 1000, 0, 10);
}
#endif