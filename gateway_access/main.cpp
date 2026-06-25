#include "TcpServer.h"
#include "Logger.h"
#include <csignal>
#include <string>

TcpServer* g_server = nullptr;

void signal_handler(int signum) {
    if (g_server) {
        g_server->stop();
    }
}

int main(){
    auto logger = GetLogger("gateway");
    logger->info("Starting gateway .... ");
    uint16_t port = 9000;

    std::string uds_path = "/tmp/gateway_data.sock";

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    TcpServer server(port, uds_path);
    g_server = &server;
    server.run();

    return 0;
}