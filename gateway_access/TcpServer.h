#ifndef __TCP_SERVER_H__
#define __TCP_SERVER_H__ 

#include "Session.h"
#include <cstdint>
#include <string>
#include <memory>
#include <unordered_map>

class TcpServer {
public:
    TcpServer(uint16_t port, const std::string& uds_path);
    ~TcpServer();

    void run();
    void stop();

private:
    int listen_fd_;
    int epfd_;
    int uds_fd_;
    bool running_;

    std::unordered_map<int, std::unique_ptr<Session>> sessions_;

    static constexpr int kMaxEvents = 1024;
    static constexpr int kListenBacklog = 128;

    void setup_listen_socket(uint16_t port);
    void setup_uds(const std::string& path);
    void handle_new_connection();
    void handle_client_data(int fd);
    void close_connection(int fd);
    static void set_nonblocking(int fd);
};

#endif