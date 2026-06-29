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
TcpServer::TcpServer(uint16_t port)
    : listen_fd_(-1)
    , epfd_(-1)
    , running_(false){
        epfd_ = epoll_create1(0);
        if (epfd_ < 0){
            throw std::runtime_error("epoll_create failed");
        }
        setup_listen_socket(port);
    }

// 析构函数
TcpServer::~TcpServer(){
    sessions_.clear();
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

        // 为每个新连接创建独立 Session，按 fd 索引，连接断开时自动回收
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

    // 防止while无线循环，设置读取最大内存
    static constexpr size_t kMaxReadSize = 65536;
    size_t total_read = 0;
    while(true){
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0){
            total_read += n;
            auto packets = session->handle_data(buf, static_cast<size_t>(n));

            for (const auto& pkt : packets){
                if(m_on_data) {
                    Tlvpacket decoded;
                    if(decode_tlv(pkt.data(), pkt.size(), decoded)){
                        InternalMessage msg;
                        msg.source_type = 0; // TCP
                        msg.node_id = 0;     // 默认节点ID为0
                        msg.tlv_type = decoded.header.type;
                        msg.payload = decoded.value;

                        m_on_data(msg);
                    } else {
                        GetLogger("gateway")->warn("Failed to decode TLV packet from fd {}", fd);
                    }
                }
            }
            if (total_read >= kMaxReadSize){
                break;
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
void TcpServer::start(){ 
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
                // 位掩码检查 fd 是否可读，数据到达则进入业务处理
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

std::string_view TcpServer::name() const{
    return "TCP-Sensor";
}