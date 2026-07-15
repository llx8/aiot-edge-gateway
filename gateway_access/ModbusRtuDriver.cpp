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
    running_ = false;
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
    tcgetattr(fd, &options);

    cfsetispeed(&options, B9600);
    cfsetospeed(&options, B9600);
    options.c_cflag |= CS8; // 8位数据位
    options.c_cflag &= ~PARENB; // 无校验位
    options.c_cflag &= ~CSTOPB; // 1位停止位
    options.c_cflag |= CLOCAL | CREAD; // 本地模式，允许读取

    options.c_cc[VMIN] = 0; // 非阻塞读取，立即返回
    options.c_cc[VTIME] = 5; // 超时时间，单位为秒
    tcsetattr(fd, TCSANOW, &options);
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
        return;
    }
    running_ = true;
    poll_thread_ = std::thread(&ModbusRtuDriver::poll_loop, this);
}

// 停止轮询线程
void ModbusRtuDriver::stop() {
    running_ = false;
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
    while (running_) {
        // 1 编码请求
        ModbusRequest req{slave_addr_, 0x03, reg_start_, reg_count_};
        auto frame = encode_request(req);
        // 2 写串口
        write(serial_fd_, frame.data(), frame.size());
        // 3 等待串口数据
        uint8_t buf[256];
        ssize_t n = read(serial_fd_, buf, sizeof(buf));
        // 4 解码
        ModbusResponse resp;
        if (n > 0 && decode_response(buf, n, resp)) {
            // 处理响应 回调函数
            InternalMessage msg;
            msg.source_type = 1;
            msg.node_id = slave_addr_;
            msg.tlv_type = 0x01;
            if (resp.registers.size() > 0) {
                msg.payload.assign(resp.registers.begin(), resp.registers.end());
            }
            m_on_data(msg);
            // 5 通知主线程
            notify_main_thread();
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