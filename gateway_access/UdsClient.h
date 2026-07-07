#pragma once

#include <string>

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
    int connect_with_backoff(int max_retries);
};
