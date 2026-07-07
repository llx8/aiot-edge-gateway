#pragma once

#include "RingBuffer.h"
#include <string>
#include <vector>
#include "InternalMessage.h"
#include <functional>
#include <unordered_set>

class EventLoop {
public:
    // 构造函数
    explicit EventLoop(const std::string& uds_path, size_t buffer_size = 65536); // 默认缓冲区大小为64KB

    // 析构函数
    ~EventLoop();

    // 开始事件循环
    bool start();

    // 停止事件循环
    void stop();

    using DataCallback = std::function<void(const InternalMessage&)>;
    void set_data_callback(DataCallback cb);

private:
    std::string uds_path_;
    int epoll_fd_;
    int listen_fd_;
    RingBuffer ring_buffer_;
    bool running_;
    std::unordered_set<int> client_fds_;
    DataCallback data_callback_;
    
    static constexpr size_t kBufferSize = 65536; // 64KB buffer size
    static constexpr int kMaxEvents = 16;

    // 设置非阻塞模式
    static void set_nonblocking(int fd);

    // 绑定监听，返回listen_fd
    int setup();

    // 接受连接
    void accept_connection();

    // 读取数据到环形缓冲区，解析返回消息列表
    std::vector<InternalMessage> handle_client_data(int client_fd);
};