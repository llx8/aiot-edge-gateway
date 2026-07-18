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
    running_.store(false);
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
        close(socket_fd_);
        socket_fd_ = -1;
        return;
    }
    running_.store(true);
    poll_thread_ = std::thread(&ModbusTcpDriver::poll_loop, this);
}

// 停止轮询线程
void ModbusTcpDriver::stop() {
    running_.store(false);
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

void ModbusTcpDriver::set_poll_interval(uint16_t interval_ms) {
    poll_interval_ms_.store(interval_ms);
    GetLogger("ModbusTcpDriver")->info("poll interval changed to {}ms", interval_ms);
}

// 主循环，轮询TCP数据并处理
void ModbusTcpDriver::poll_loop() {
    while (running_.load()) {
        // 1 编码请求
        ModbusRequest req{slave_addr_, 0x03, reg_start_, reg_count_};
        auto frame = encode_tcp_request(trans_id_, req);
        trans_id_++;
        // 2 写TCP
        ssize_t wr = write(socket_fd_, frame.data(), frame.size());
        if (wr != static_cast<ssize_t>(frame.size())) {
            GetLogger("ModbusTcpDriver")->error("write incomplete: {}/{} bytes, errno={}",
                wr, frame.size(), wr < 0 ? strerror(errno) : "short write");
            reconnect();
            continue;
        }
        // 3 等待TCP响应（带超时），追加到接收缓冲区
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(socket_fd_, &rfds);
        struct timeval tv{0, 500000};  // 500ms
        int sel_ret;
        do {
            sel_ret = select(socket_fd_ + 1, &rfds, nullptr, nullptr, &tv);
        } while (sel_ret < 0 && errno == EINTR);  // W7: 信号打断不应误判为对端坏掉
        if (sel_ret > 0) {
            uint8_t buf[256];
            ssize_t n = read(socket_fd_, buf, sizeof(buf));
            if (n > 0) {
                recv_buf_.insert(recv_buf_.end(), buf, buf + n);
                // 防止异常数据导致内存膨胀
                if (recv_buf_.size() > kMaxRecvBuf) {
                    recv_buf_.clear();
                    reconnect();
                    continue;
                }
            } else {
                recv_buf_.clear();
                reconnect();
                continue;
            }
        } else {
            // select 超时
            recv_buf_.clear();
            reconnect();
            continue;
        }
        // 4 从缓冲区提取完整帧（MBAP 头 6 字节，其中 offset 4-5 为长度字段）
        bool decoded_any = false;
        while (recv_buf_.size() >= 6) {
            // MBAP 长度字段 = unit_id(1) + PDU 长度
            uint16_t mbap_len = (static_cast<uint16_t>(recv_buf_[4]) << 8) | recv_buf_[5];
            // 长度合理性检查：Modbus TCP 最大帧约 260 字节
            if (mbap_len < 1 || mbap_len > 256) {
                recv_buf_.clear();
                reconnect();
                break;
            }
            size_t total_len = 6 + mbap_len;
            if (recv_buf_.size() < total_len) break;  // 帧不完整，等下一轮 read

            ModbusResponse resp;
            if (decode_tcp_response(recv_buf_.data(), total_len, resp)) {
                InternalMessage msg;
                msg.source_type = 0;  // 0 = Modbus
                msg.node_id = slave_addr_;
                msg.tlv_type = 0x01;
                if (resp.registers.size() > 0) {
                    // 寄存器为 uint16，payload 为字节流，须按大端序列化
                    msg.payload.resize(resp.registers.size() * 2);
                    for (size_t i = 0; i < resp.registers.size(); ++i) {
                        msg.payload[i * 2]     = static_cast<uint8_t>(resp.registers[i] >> 8);
                        msg.payload[i * 2 + 1] = static_cast<uint8_t>(resp.registers[i] & 0xFF);
                    }
                }
                m_on_data(msg);
                notify_main_thread();
                decoded_any = true;
            }
            // 从缓冲区移除已处理的帧
            recv_buf_.erase(recv_buf_.begin(), recv_buf_.begin() + total_len);
        }
        // 5 等下一轮
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms_.load()));
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
    if (flags < 0) {
        GetLogger("ModbusTcpDriver")->error("fcntl F_GETFL failed: {}", strerror(errno));
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
    
    // 3. 连接远端 PLC
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, ip_.c_str(), &addr.sin_addr) <= 0) {
        GetLogger("ModbusTcpDriver")->error("inet_pton failed for {}", ip_);
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
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
    int sel_ret;
    do {
        sel_ret = select(socket_fd_ + 1, nullptr, &wfds, nullptr, &tv);
    } while (sel_ret < 0 && errno == EINTR);
    if (sel_ret <= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    // select 可写不代表连接成功（连接失败时 socket 也会变可写并带 pending error），
    // 必须用 SO_ERROR 确认，否则会误判已连接，后续 write/read 失败空转重连
    int err = 0;
    socklen_t errlen = sizeof(err);
    if (getsockopt(socket_fd_, SOL_SOCKET, SO_ERROR, &err, &errlen) < 0 || err != 0) {
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    // 5. 恢复阻塞模式：poll_loop 中 write/read 依赖阻塞语义且未检查返回值
    fcntl(socket_fd_, F_SETFL, flags & ~O_NONBLOCK);
    return true;
}

void ModbusTcpDriver::reconnect() {
    if (socket_fd_ != -1) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    GetLogger("ModbusTcpDriver")->warn("Reconnecting to {}:{}", ip_, port_);
    int backoff_ms = 100;
    while (running_.load() && !connect_to_server()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        if (backoff_ms < 2000) backoff_ms *= 2;  // 最大 2s
    }
    if (running_) {
        GetLogger("ModbusTcpDriver")->info("Reconnected to {}:{}", ip_, port_);
    }
}


#ifndef MODBUS_TCP_STATIC  // 静态链接时跳过(避免 dlopen 工厂函数冲突)
extern "C" ISensorDriver* create_driver() {
    return new ModbusTcpDriver("127.0.0.1", 5020, 1, 1000, 0, 10);
}
#endif