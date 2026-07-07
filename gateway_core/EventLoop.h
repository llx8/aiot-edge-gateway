#pragma once

#include "RingBuffer.h"
#include <string>
#include <vector>
#include "InternalMessage.h"
#include <functional>
#include <unordered_set>

class EventLoop {
public:
    // 构造函数：支持多个 UDS 路径
    explicit EventLoop(const std::vector<std::string>& uds_paths, size_t buffer_size = 65536);

    // 析构函数
    ~EventLoop();

    // 开始事件循环
    bool start();

    // 停止事件循环
    void stop();

    using DataCallback = std::function<void(const InternalMessage&)>;
    void set_data_callback(DataCallback cb);

private:
    std::vector<std::string> uds_paths_;
    int epoll_fd_;
    std::unordered_set<int> listen_fds_;   // 多个监听 fd
    RingBuffer ring_buffer_;
    bool running_;
    std::unordered_set<int> client_fds_;
    DataCallback data_callback_;
    
    static constexpr size_t kBufferSize = 65536;
    static constexpr int kMaxEvents = 16;

    static void set_nonblocking(int fd);

    // 绑定所有 UDS 路径
    void setup();

    // 接受连接
    void accept_connection(int listen_fd);

    // 读取数据到环形缓冲区，解析返回消息列表
    std::vector<InternalMessage> handle_client_data(int client_fd);
};
