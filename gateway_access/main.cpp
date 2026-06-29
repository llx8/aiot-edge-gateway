#include "TcpServer.h"
#include "Logger.h"
#include <csignal>
#include <cstring>
#include <string>
#include "InternalMessage.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>
#include "Config.h"

// 全局指针，供信号处理函数访问 TcpServer 实例
TcpServer* g_server = nullptr;

// 信号处理函数
void signal_handler(int signum) {
    if (g_server) {
        g_server->stop();
    }
}

// 连接进程B，给进程B传输数据
static int connect_uds(const std::string& path, int max_retries = 30){
    // 实现延迟连接
    int backoff_ms = 100;

    // 重试次数，最多30次 总等待时间位51秒
    for(int retry = 0; retry < max_retries; retry++) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            GetLogger("gateway")->error("UDS socket failed: {}", strerror(errno));
            return -1;
        }

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path));

        if(connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0){
            // 连接上了，返回fd
            GetLogger("gateway")->info("UDS connected to {} (retry={})", path, retry);
            return fd;
        }
    
        // 没连接上，重试
        close(fd);
        GetLogger("gateway")->warn("UDS connect failed (retry={}/{}): {}",retry, max_retries, strerror(errno));
    
        // 延迟重试，防止日志太多
        usleep(backoff_ms * 1000);
        // 最大延迟时间为两秒
        if (backoff_ms < 2000) {
            backoff_ms *= 2;
        }
    }
    return -1;
}

int main(){
    // 初始化日志
    auto logger = GetLogger("gateway");
    logger->info("Starting gateway .... ");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 读配置文件，找不到用默认值
    auto config = load_config("conf/gateway.conf");
    uint16_t port = static_cast<uint16_t>(std::stoi(config["tcp"]["port"]));
    std::string uds_path = config["uds"]["data_path"];

    logger->info("Config: port={}, uds_path={}", port, uds_path);

    int uds_fd = connect_uds(uds_path);

    TcpServer server(port);
    g_server = &server;

    // 给回调函数添加执行函数
    server.set_data_callback([uds_fd](const InternalMessage& msg){
        auto encoded = encode_internal_msg(msg);
        if (uds_fd >= 0){
            write(uds_fd, encoded.data(), encoded.size());
        }
    });

    // 启动tcp服务
    server.start();
    if (uds_fd >= 0) close(uds_fd);

    return 0;
}