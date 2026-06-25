#include "TcpServer.h"
#include "Logger.h"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <stdexcept>

// 构造函数
TcpServer::TcpServer(uint16_t port, const std::string& uds_path)
    : listen_fd_(-1)
    , epfd_(-1)
    , uds_fd_(-1)
    , running_(false){
        epfd_ = epoll_create1(0);
        if (epfd_ < 0){
            throw std::runtime_error("epoll_create failed");
        }
        setup_listen_socket(port);
        setup_uds(uds_path);
    }

// 析构函数
TcpServer::~TcpServer(){
    sessions_.clear();
    if (uds_fd_ >= 0) close(uds_fd_);
    if (listen_fd_ >= 0) close(listen_fd_);
    if (epfd_ >= 0) close(epfd_);
}

// 设置非阻塞
void TcpServer::set_nonblocking(int fd){
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 监听
void TcpServer::setup_listen_socket(uint16_t port){
    // socket
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0){
        throw std::runtime_error("socket failed");
    }
    // 重启秒绑
    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 非阻塞
    set_nonblocking(listen_fd_);

    // bind
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        throw std::runtime_error("bind failed");
    }

    // listen
    if (listen(listen_fd_, kListenBacklog) < 0){
        throw std::runtime_error("listen failed");
    }

    // 注册epoll
    struct epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = listen_fd_;
    epoll_ctl(epfd_, EPOLL_CTL_ADD, listen_fd_, &ev);
    GetLogger("gateway")->info("Listening on port {}", port);
}

// uds连接进程B
void TcpServer::setup_uds(const std::string& uds_path){ 
    uds_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (uds_fd_ < 0){
        throw std::runtime_error("socket failed");
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, uds_path.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(uds_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        GetLogger("gateway")->error("connect failed");
        close(uds_fd_);
        uds_fd_ = -1;
    }
    else{
        GetLogger("gateway")->info("UDS connected to {}", uds_path);
    }
}

// 处理新连接
void TcpServer::handle_new_connection(){ 
    while(true){
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0){
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            GetLogger("gateway")->error("accept error : {}", strerror(errno));
            break;
        }

        set_nonblocking(client_fd);

        struct epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        ev.data.fd = client_fd;
        epoll_ctl(epfd_, EPOLL_CTL_ADD, client_fd, &ev);

        sessions_[client_fd] = std::make_unique<Session>(client_fd);

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));

        GetLogger("gateway")->info("New connection from {}:{}", ip, ntohs(client_addr.sin_port));
    }
}

// 处理数据
void TcpServer::handle_client_data(int fd){
    auto it = sessions_.find(fd);
    if (it == sessions_.end()) return;

    Session* session = it->second.get();
    uint8_t buf[4096];
    bool alive = true;

    while(true){
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0){
            auto packets = session->handle_data(buf, static_cast<size_t>(n));

            for (const auto& pkt : packets){
                if (uds_fd_ >= 0){
                    ssize_t sent = write(uds_fd_, pkt.data(), pkt.size());
                    if (sent < 0){
                        GetLogger("gateway")->error("UDS write : {}", strerror(errno));
                    }
                }
            }
        }
        else if (n == 0){
            alive = false;
            break;
        }
        else{
            if (errno == EAGAIN || errno == EWOULDBLOCK){
                break;
            }
            alive = false;
            break;
        }
    }
    if (!alive) close_connection(fd);
}

// 关闭连接
void TcpServer::close_connection(int fd){
    epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
    sessions_.erase(fd);
    close(fd);
    GetLogger("gateway")->info("Connection closed: fd = {}", fd);
}

// 主循环
void TcpServer::run(){ 
    running_ = true;
    auto logger = GetLogger("gateway");
        
    struct epoll_event events[kMaxEvents];
    while(running_){ 
        int n = epoll_wait(epfd_, events, kMaxEvents, 1000);
        if (n < 0) {
            if (errno == EINTR) continue;
            logger->error("epoll_wait error : {}", strerror(errno));
            break;
        }
        for (int i = 0; i < n; i++){ 
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (fd == listen_fd_){
                handle_new_connection();
            }
            else if (ev & (EPOLLERR | EPOLLRDHUP | EPOLLHUP)){
                close_connection(fd);
            }
            else if (ev & EPOLLIN){
                handle_client_data(fd);
            }
        }
    }
    logger->info("gateway stopped");
}

// 停止
void TcpServer::stop(){
    running_ = false;
}