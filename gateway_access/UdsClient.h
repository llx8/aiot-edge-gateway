#pragma once

#include <string>
#include <mutex>

class UdsClient {
public:
    UdsClient(const std::string& path, int max_retries = 30);
    ~UdsClient();

    int fd() const { return fd_; }
    bool is_connected() const { return fd_ >= 0; }
    ssize_t write(const void* buf, size_t len);

private:
    int fd_;
    std::string path_;
    // 同一 fd 跨线程并发 write 时单条 SEQPACKET 数据报本身原子，
    // 但 datagram 顺序在接收端可能交错。心跳 / 告警 / JPEG 走多线程
    // 都用此 fd，加锁顺手串行化顺序，避免告警/JPEG 错配。
    std::mutex write_mutex_;
    int connect_with_backoff(int max_retries);
};
