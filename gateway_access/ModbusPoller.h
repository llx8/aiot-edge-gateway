#pragma once
#include "ISouthbound.h"
#include <string>
#include <thread>

class ModbusPoller : public ISouthbound {
public:
    // 构造函数
    ModbusPoller(const std::string& serial_port, uint8_t slave_addr, uint16_t poll_interval_ms, uint16_t reg_start, uint16_t reg_count);
    // 析构函数
    ~ModbusPoller();
    // 启动轮询线程
    void start() override;
    // 停止轮询线程
    void stop() override;
    // 获取轮询线程名称
    std::string_view name() const override;
    int event_fd() const; // 获取事件文件描述符
private:
    std::string serial_port_; // 串口路径
    int serial_fd_ = -1; // 串口文件描述符
    int event_fd_ = -1; // 事件文件描述符
    uint8_t slave_addr_; // 从站地址
    uint16_t poll_interval_ms_; // 轮询间隔，单位毫秒
    uint16_t reg_start_, reg_count_; // 注册器起始地址和数量
    bool running_; // 是否运行中
    std::thread poll_thread_; // 轮询线程

    // 打开串口文件
    bool open_serial();
    // 轮询主循环
    void poll_loop();
    // write (event_fd)
    void notify_main_thread();
};