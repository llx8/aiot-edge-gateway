#include "ModbusTcpDriver.h"
#include <chrono>
#include <fcntl.h>
#include <thread>
#include <unistd.h>
#include <sys/select.h>
#include "Logger.h"
#include "ModbusRtu.h"
#include "InternalMessage.h"
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

// 构造函数
ModbusTcpDriver::ModbusTcpDriver(const std::string& ip, uint16_t port, uint8_t slave_addr, uint16_t poll_interval_ms, uint16_t reg_start, uint16_t reg_count)
    : ip_(ip)
    , port_(port)
    , slave_addr_(slave_addr)
    , poll_interval_ms_(poll_interval_ms)
    , reg_start_(reg_start)
    , reg_count_(reg_count)
{
    running_ = false;
}

// 析构函数
ModbusTcpDriver::~ModbusTcpDriver()
{
    stop();
}

// write (event_fd)
void ModbusTcpDriver::notify_main_thread(){
    uint64_t cnt = 1;
    write(event_fd_, &cnt, sizeof(cnt));
}

// 启动轮询线程
void ModbusTcpDriver::start() {
    if (!connect_to_server()) {
        GetLogger("ModbusTcpDriver")->error("Failed to connect to server: {}:{}", ip_, port_);
        return;
    }
    event_fd_ = eventfd(0, EFD_NONBLOCK);
    if (event_fd_ < 0) {
        GetLogger("ModbusTcpDriver")->error("Failed to create eventfd: {}", strerror(errno));
        return;
    }
    running_ = true;
    poll_thread_ = std::thread(&ModbusTcpDriver::poll_loop, this);
}

// 停止轮询线程
void ModbusTcpDriver::stop() {
    running_ = false;
    if (poll_thread_.joinable()) {
        poll_thread_.join();
        }
    if (socket_fd_ != -1) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    if (event_fd_ != -1) {
        close(event_fd_);
        event_fd_ = -1;
    }
}

int ModbusTcpDriver::event_fd() const {
    return event_fd_;
}

std::string_view ModbusTcpDriver::name() const {
    return "Modbus-TCP";
}

// 主循环，轮询TCP数据并处理
void ModbusTcpDriver::poll_loop() {
    while (running_) {
        // 1 编码请求
        ModbusRequest req{slave_addr_, 0x03, reg_start_, reg_count_};
        auto frame = encode_tcp_request(trans_id_, req);
        trans_id_++;
        // 2 写TCP
        write(socket_fd_, frame.data(), frame.size());
        // 3 等待TCP响应（带超时）
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(socket_fd_, &rfds);
        struct timeval tv{0, 500000};  // 500ms
        uint8_t buf[256];
        ssize_t n = -1;
        if (select(socket_fd_ + 1, &rfds, nullptr, nullptr, &tv) > 0) {
            n = read(socket_fd_, buf, sizeof(buf));
        }
        if (n <= 0) {
            reconnect();
            continue;
        }
        // 4 解码
        ModbusResponse resp;
        if (decode_tcp_response(buf, n, resp)) {
            InternalMessage msg;
            msg.source_type = 1;
            msg.node_id = slave_addr_;
            msg.tlv_type = 0x01;
            if (resp.registers.size() > 0) {
                msg.payload.assign(resp.registers.begin(), resp.registers.end());
            }
            m_on_data(msg);
            notify_main_thread();
        }
        // 5 等下一轮
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms_));
    }
}

bool ModbusTcpDriver::connect_to_server() {
    // 1. 创建 TCP socket
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        GetLogger("ModbusTcpDriver")->error("socket failed: {}", strerror(errno));
        return false;
    }
    
    // 2. 设置非阻塞模式
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
    
    // 3. 连接远端 PLC
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    inet_pton(AF_INET, ip_.c_str(), &addr.sin_addr);
    
    int ret = connect(socket_fd_, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    // 4. select 等连接完成
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(socket_fd_, &wfds);
    struct timeval tv{3, 0};  // 3 秒超时
    if (select(socket_fd_ + 1, nullptr, &wfds, nullptr, &tv) <= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    return true;
}

void ModbusTcpDriver::reconnect() {
    if (socket_fd_ != -1) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    GetLogger("ModbusTcpDriver")->warn("Reconnecting to {}:{}", ip_, port_);
    int backoff_ms = 100;
    while (running_ && !connect_to_server()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        if (backoff_ms < 2000) backoff_ms *= 2;  // 最大 2s
    }
    if (running_) {
        GetLogger("ModbusTcpDriver")->info("Reconnected to {}:{}", ip_, port_);
    }
}


extern "C" ISensorDriver* create_driver() {
    return new ModbusTcpDriver("127.0.0.1", 502, 1, 1000, 0, 10);
}