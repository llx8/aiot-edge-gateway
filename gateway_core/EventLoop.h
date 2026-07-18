#pragma once

#include <string>
#include <vector>
#include <atomic>
#include "InternalMessage.h"
#include <functional>
#include <unordered_set>
#include <unordered_map>

class EventLoop {
public:
    // 构造函数：支持多个 UDS 路径
    explicit EventLoop(const std::vector<std::string>& uds_paths);

    // 析构函数
    ~EventLoop();

    // 开始事件循环
    bool start();

    // 停止事件循环
    void stop();

    using DataCallback = std::function<void(int fd, const InternalMessage&)>;
    void set_data_callback(DataCallback cb);

    using DisconnectCallback = std::function<void(int fd)>;
    void set_disconnect_callback(DisconnectCallback cb);

    using FdCallback = std::function<void(int fd)>;
    void add_external_fd(int fd, FdCallback cb);

    // 接受到C进程的消息的回调函数
    void set_monitor_path(const std::string& path);
    using FdReceivedCallback = std::function<void(int fd)>;
    void set_fd_received_callback(FdReceivedCallback cb);

    // 向 monitor 进程发送数据（JPEG 快照等）
    void send_to_monitor(const uint8_t* data, size_t len);
private:
    std::vector<std::string> uds_paths_;
    int epoll_fd_;
    std::unordered_set<int> listen_fds_;   // 多个监听 fd
    std::atomic<bool> running_{false};
    std::unordered_set<int> client_fds_;
    std::unordered_map<int, FdCallback> fd_callbacks_;
    DataCallback data_callback_;
    DisconnectCallback disconnect_callback_;

    std::string monitor_path_;
    std::unordered_set<int> monitor_listen_fds_;
    std::unordered_set<int> monitor_client_fds_;
    // 从 monitor 客户端接收消息（含 SCM_RIGHTS fd 处理），返回读取的字节数
    ssize_t receive_fd(int client_fd, std::vector<uint8_t>& raw_data);
    FdReceivedCallback fd_received_callback_;
    
    static constexpr int kMaxEvents = 16;

    static void set_nonblocking(int fd);

    // 绑定所有 UDS 路径
    void setup();

    // 接受连接
    void accept_connection(int listen_fd);

    // 读取数据到环形缓冲区，解析返回消息列表
    std::vector<InternalMessage> handle_client_data(int client_fd);
};
