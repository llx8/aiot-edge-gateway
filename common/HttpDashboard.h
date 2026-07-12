#pragma once

#include <string>
#include <thread>
#include <atomic>

// 轻量 HTTP 监控面板 — 读取共享内存的 ShmBlock 通过 HTTP/JSON 对外暴露
class HttpDashboard {
public:
    HttpDashboard();
    ~HttpDashboard();

    bool start(int port, int shm_key);
    void stop();

private:
    void server_thread(int port, int shm_key);
    void handle_client(int client_fd, int shm_key);

    std::string read_shm_metrics(int shm_key);
    std::string http_response(const std::string& status, const std::string& content_type,
                              const std::string& body);
    std::string json_escape(const std::string& s);

    std::atomic<bool> running_{false};
    std::thread thread_;
    int listen_fd_{-1};
};
