#include "UdsClient.h"
#include "Logger.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>

UdsClient::UdsClient(const std::string& path, int max_retries)
    : fd_(-1)
    , path_(path)
{
    fd_ = connect_with_backoff(max_retries);
}

UdsClient::~UdsClient() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

ssize_t UdsClient::write(const void* buf, size_t len) {
    if (fd_ < 0) return -1;
    return ::write(fd_, buf, len);
}

int UdsClient::connect_with_backoff(int max_retries) {
    int backoff_ms = 100;

    for (int retry = 0; retry < max_retries; retry++) {
        int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (fd < 0) {
            GetLogger("UdsClient")->error("socket failed: {}", strerror(errno));
            return -1;
        }

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path));

        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            GetLogger("UdsClient")->info("Connected to {} (retry={})", path_, retry);
            return fd;
        }

        close(fd);
        GetLogger("UdsClient")->warn("Connect failed (retry={}/{}): {}", retry, max_retries, strerror(errno));

        usleep(backoff_ms * 1000);
        if (backoff_ms < 2000) {
            backoff_ms *= 2;
        }
    }
    return -1;
}
