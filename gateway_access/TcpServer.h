#ifndef __TCP_SERVER_H__
#define __TCP_SERVER_H__ 

#include "Session.h"
#include <cstdint>
#include <string>
#include <memory>
#include <unordered_map>
#include "ISouthbound.h"

class TcpServer : public ISouthbound {
public:
    TcpServer(uint16_t port);
    ~TcpServer();

    void start() override;
    void stop() override;
    std::string_view name() const override;

private:
    int listen_fd_;
    int epfd_;
    bool running_;

    std::unordered_map<int, std::unique_ptr<Session>> sessions_;

    static constexpr int kMaxEvents = 1024;
    static constexpr int kListenBacklog = 128;

    void setup_listen_socket(uint16_t port);
    void handle_new_connection();
    void handle_client_data(int fd);
    void close_connection(int fd);
    static void set_nonblocking(int fd);
};

#endif