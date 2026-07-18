#pragma once
#include "ISensorDriver.h"
#include <string>
#include <thread>
#include <atomic>
#include <cstdint>
#include <vector>

class ModbusTcpDriver : public ISensorDriver {
public:
    ModbusTcpDriver(const std::string& ip, uint16_t port, uint8_t slave_addr, uint16_t poll_interval_ms, uint16_t reg_start, uint16_t reg_count);
    ~ModbusTcpDriver();
    void start() override;
    void stop() override;
    std::string_view name() const override;
    void set_poll_interval(uint16_t interval_ms) override;
    int event_fd() const;
private:
    bool connect_to_server();      // 创建 socket + O_NONBLOCK + connect
    void poll_loop();              // 轮询主循环
    void notify_main_thread();     // write eventfd
    void reconnect();              // 断连重连
    // 成员变量
    std::string ip_;  uint16_t port_;
    uint16_t trans_id_ = 0;
    int socket_fd_ = -1;
    int event_fd_ = -1;
    uint8_t slave_addr_ = 0;
    std::atomic<uint16_t> poll_interval_ms_{0};
    uint16_t reg_start_ = 0;
    uint16_t reg_count_ = 0;
    std::atomic<bool> running_{false};
    std::thread poll_thread_ = {};
    std::vector<uint8_t> recv_buf_;     // TCP 流式接收缓冲区（MBAP 分帧）
    static constexpr size_t kMaxRecvBuf = 65536;  // 防止异常数据导致内存膨胀
};