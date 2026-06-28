#include "EventLoop.h"
#include "Logger.h"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <stdexcept>

// 设置非阻塞模式
void EventLoop::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 构造函数
EventLoop::EventLoop(const std::string& uds_path, size_t buffer_size)
    : uds_path_(uds_path)
    , epoll_fd_(-1)
    , listen_fd_(-1)
    , ring_buffer_(buffer_size)
    , running_(false)
    , client_fd_(-1) {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        throw std::runtime_error("epoll_create failed");
    }
}

// 析构函数
EventLoop::~EventLoop() {
    if (listen_fd_ >= 0) close(listen_fd_);
    if (epoll_fd_ >= 0) close(epoll_fd_);
    if (client_fd_ >= 0) close(client_fd_);
}

// 绑定监听，返回listen_fd
int EventLoop::setup() {
    // 创建UNIX域套接字
    listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        throw std::runtime_error("socket failed");
    }

    // 设置非阻塞
    set_nonblocking(listen_fd_);

    // 删除已存在的文件
    unlink(uds_path_.c_str());

    // 绑定路径
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, uds_path_.c_str(), sizeof(addr.sun_path) - 1);
    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        throw std::runtime_error("bind failed");
    }

    // 开始监听
    if (listen(listen_fd_, SOMAXCONN) < 0) {
        throw std::runtime_error("listen failed");
    }

    // 注册到epoll
    struct epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = listen_fd_;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev);

    GetLogger("gateway")->info("Listening on UDS path {}", uds_path_);
    return listen_fd_;
}

// 接受连接
int EventLoop::accept_connection() {
    while (true) {
        int fd = accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // 没有更多连接
            } else {
                GetLogger("gateway")->error("accept error: {}", strerror(errno));
                return -1;
            }
        }
        set_nonblocking(fd);

        client_fd_ = fd;

        // 注册到epoll
        struct epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        ev.data.fd = fd;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
    }
    return client_fd_;
}

// 读取数据到环形缓冲区，解析返回消息列表
std::vector<InternalMessage> EventLoop::handle_client_data(int client_fd) {
    uint8_t buffer[4096];
    size_t total_read = 0;
    std::vector<InternalMessage> messages;

    while(true) {
        ssize_t n = read(client_fd, buffer, sizeof(buffer));
        if (n > 0) {
            total_read += n;
            ring_buffer_.append(buffer, n);

            // 尝试解码消息
            while(ring_buffer_.available_to_read() > 0) {
                auto result = decode_internal_msg(ring_buffer_.read_ptr(), ring_buffer_.available_to_read());
                if (!result.ok) {
                    break; // 无法解码更多消息
                }
                messages.push_back(std::move(result.msg));
                ring_buffer_.consume(result.consumed);
            }
            if (total_read >= kBufferSize) {
                break; // 防止无限循环
            }
        }
        else if (n == 0) {
            // 客户端关闭消息
            close(client_fd);
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
            client_fd_ = -1;
            GetLogger("gateway")->info("Client disconnected");
            break;
        }
        else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // 没有更多数据
            } else {
                GetLogger("gateway")->error("read error: {}", strerror(errno));
                close(client_fd);
                epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
                client_fd_ = -1;
                break;
            }
        }
    }
    return messages;
}

// 开始事件循环
bool EventLoop::start() {
    if (setup() < 0) {
        return false;
    }
    running_ = true;

    struct epoll_event events[kMaxEvents];
    while (running_) {
        int n = epoll_wait(epoll_fd_, events, kMaxEvents, 1000);
        if (n < 0) {
            if (errno == EINTR) continue;
            GetLogger("gateway")->error("epoll_wait error: {}", strerror(errno));
            break;
        }
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (fd == listen_fd_) {
                accept_connection();
            }
            else if (ev & (EPOLLERR | EPOLLRDHUP | EPOLLHUP)) {
                close(fd);
                epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
                GetLogger("gateway")->info("Client disconnected");
                client_fd_ = -1;
            }
            else if (ev & EPOLLIN) {
                auto messages = handle_client_data(fd);
                for (const auto& msg : messages) {
                    // 处理消息
                    if (data_callback_) {
                        data_callback_(msg);
                    }
                }
            }
        }
    }
    return true;
}

// 停止事件循环
void EventLoop::stop() {
    running_ = false;
}

// 设置数据回调
void EventLoop::set_data_callback(DataCallback cb) {
    data_callback_ = std::move(cb);
}