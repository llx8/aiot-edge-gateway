#pragma once
#include "ISensorDriver.h"
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

class ModbusRtuDriver : public ISensorDriver {
public:
    // 构造函数
    ModbusRtuDriver(const std::string& serial_port, uint8_t slave_addr, uint16_t poll_interval_ms, uint16_t reg_start, uint16_t reg_count);
    // 析构函数
    ~ModbusRtuDriver();
    // 启动轮询线程
    void start() override;
    // 停止轮询线程
    void stop() override;
    // 获取轮询线程名称
    std::string_view name() const override;
    // 动态修改轮询间隔
    void set_poll_interval(uint16_t interval_ms) override;
    int event_fd() const; // 获取事件文件描述符
private:
    std::string serial_port_; // 串口路径
    int serial_fd_ = -1; // 串口文件描述符
    int event_fd_ = -1; // 事件文件描述符
    uint8_t slave_addr_; // 从站地址
    std::atomic<uint16_t> poll_interval_ms_{0}; // 轮询间隔，原子变量支持 RPC 热修改
    uint16_t reg_start_, reg_count_; // 注册器起始地址和数量
    std::atomic<bool> running_{false}; // 是否运行中
    std::thread poll_thread_; // 轮询线程

    // 超时剔除与复活（设计:102：连续3次超时挂起，30s尝试复活一次）
    int consecutive_timeouts_ = 0;
    bool suspended_ = false;
    std::chrono::steady_clock::time_point last_revival_attempt_;
    static constexpr int kMaxTimeouts = 3;
    static constexpr int kRevivalIntervalSec = 30;

    // 打开串口文件
    bool open_serial();
    // 轮询主循环
    void poll_loop();
    // write (event_fd)
    void notify_main_thread();
};