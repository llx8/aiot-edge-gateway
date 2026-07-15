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

void EventLoop::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

EventLoop::EventLoop(const std::vector<std::string>& uds_paths, size_t buffer_size)
    : uds_paths_(uds_paths)
    , epoll_fd_(-1)
    , ring_buffer_(buffer_size)
    , running_(false){
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        throw std::runtime_error("epoll_create failed");
    }
}

EventLoop::~EventLoop() {
    for (auto fd : listen_fds_) close(fd);
    if (epoll_fd_ >= 0) close(epoll_fd_);
    for (auto fd : client_fds_) close(fd);
    for (auto fd : monitor_client_fds_) close(fd);
}



void EventLoop::setup() {
    for (const auto& path : uds_paths_) {
        int listen_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (listen_fd < 0) {
            throw std::runtime_error("socket failed for " + path);
        }

        set_nonblocking(listen_fd);
        unlink(path.c_str());

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            throw std::runtime_error("bind failed for " + path);
        }

        if (listen(listen_fd, SOMAXCONN) < 0) {
            throw std::runtime_error("listen failed for " + path);
        }

        struct epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = listen_fd;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd, &ev);

        listen_fds_.insert(listen_fd);
        GetLogger("gateway_core")->info("Listening on UDS path {}", path);
    }
}

void EventLoop::accept_connection(int listen_fd) {
    while (true) {
        int fd = accept(listen_fd, nullptr, nullptr);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                GetLogger("gateway_core")->error("accept error: {}", strerror(errno));
                return;
            }
        }
        set_nonblocking(fd);
        if (monitor_listen_fds_.count(listen_fd)) {
            monitor_client_fds_.insert(fd);
        } else {
            client_fds_.insert(fd);
        }

        struct epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        ev.data.fd = fd;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
    }
}

void EventLoop::receive_fd(int fd) {
    char dummy;
    struct iovec iov = {&dummy, 1};

    char cmsg_buf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg = {};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = CMSG_SPACE(sizeof(int));
    
    if (recvmsg(fd, &msg, 0) < 0) {
        return;
    }

    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET 
        && cmsg->cmsg_type == SCM_RIGHTS) {
        int received_fd = *(int*)CMSG_DATA(cmsg);
        if (fd_received_callback_) {
            fd_received_callback_(received_fd);
        }
    }
}

std::vector<InternalMessage> EventLoop::handle_client_data(int client_fd) {
    if (monitor_client_fds_.count(client_fd)) {
        receive_fd(client_fd);
        return {};
    }
    uint8_t buffer[4096];
    size_t total_read = 0;
    std::vector<InternalMessage> messages;

    while(true) {
        ssize_t n = read(client_fd, buffer, sizeof(buffer));
        if (n > 0) {
            total_read += n;
            ring_buffer_.append(buffer, n);

            while(ring_buffer_.available_to_read() > 0) {
                auto result = decode_internal_msg(ring_buffer_.read_ptr(), ring_buffer_.available_to_read());
                if (!result.ok) {
                    break;
                }
                messages.push_back(std::move(result.msg));
                ring_buffer_.consume(result.consumed);
            }
            if (total_read >= kBufferSize) {
                break;
            }
        }
        else if (n == 0) {
            close(client_fd);
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
            client_fds_.erase(client_fd);
            GetLogger("gateway_core")->info("Client disconnected");
            break;
        }
        else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                GetLogger("gateway_core")->error("read error: {}", strerror(errno));
                close(client_fd);
                epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
                client_fds_.erase(client_fd);
                break;
            }
        }
    }
    return messages;
}

bool EventLoop::start() {
    setup();
    running_ = true;

    struct epoll_event events[kMaxEvents];
    while (running_) {
        int n = epoll_wait(epoll_fd_, events, kMaxEvents, 1000);
        if (n < 0) {
            if (errno == EINTR) continue;
            GetLogger("gateway_core")->error("epoll_wait error: {}", strerror(errno));
            break;
        }
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (listen_fds_.count(fd) || monitor_listen_fds_.count(fd)) {
                accept_connection(fd);
                continue;
            }

            if (fd_callbacks_.count(fd)) {
                // 消费eventfd
                uint64_t val;
                while (read(fd, &val, sizeof(val)) > 0) {}
                fd_callbacks_[fd](fd);
            }
            else if (ev & (EPOLLERR | EPOLLRDHUP | EPOLLHUP)) {
                close(fd);
                epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
                if (monitor_client_fds_.count(fd)) {
                    GetLogger("gateway_core")->info("Monitor client disconnected");
                    monitor_client_fds_.erase(fd);
                } else {
                    GetLogger("gateway_core")->info("Client disconnected");
                    client_fds_.erase(fd);
                }
            }
            else if (ev & EPOLLIN) {
                auto messages = handle_client_data(fd);
                for (const auto& msg : messages) {
                    if (data_callback_) {
                        data_callback_(fd, msg);
                    }
                }
            }
        }
    }
    return true;
}

void EventLoop::stop() {
    running_ = false;
}

void EventLoop::set_data_callback(DataCallback cb) {
    data_callback_ = std::move(cb);
}

void EventLoop::add_external_fd(int fd, FdCallback cb) {
    fd_callbacks_[fd] = std::move(cb);
    // 通知epoll监听新fd
    struct epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
}

void EventLoop::set_fd_received_callback(FdReceivedCallback cb) {
    fd_received_callback_ = std::move(cb);
}

void EventLoop::set_monitor_path(const std::string& path) {
    monitor_path_ = path;
    int listen_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (listen_fd < 0) {
        throw std::runtime_error("socket failed for monitor: " + path);
    }
    set_nonblocking(listen_fd);
    unlink(path.c_str());

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        throw std::runtime_error("bind failed for monitor: " + path);
    }
    if (listen(listen_fd, SOMAXCONN) < 0) {
        throw std::runtime_error("listen failed for monitor: " + path);
    }

    struct epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = listen_fd;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd, &ev);

    monitor_listen_fds_.insert(listen_fd);
    GetLogger("gateway_core")->info("Monitor UDS path {}", path);
}

void EventLoop::send_to_monitor(const uint8_t* data, size_t len) {
    // 向所有已连接的 monitor 客户端广播
    for (int fd : monitor_client_fds_) {
        ssize_t written = write(fd, data, len);
        if (written < 0 && (errno == EPIPE || errno == ECONNRESET)) {
            GetLogger("gateway_core")->warn("Monitor client disconnected during send");
            close(fd);
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
            monitor_client_fds_.erase(fd);
        }
    }
}