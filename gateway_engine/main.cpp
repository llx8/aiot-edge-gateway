#include "Logger.h"
#include <csignal>
#include <unistd.h>
#include <fstream>
#include <sys/stat.h>

static volatile sig_atomic_t g_running = 1;

void signal_handler(int signum) {
    g_running = 0;
}

int main() {
    auto logger = GetLogger("gateway_engine");
    logger->info("Starting gateway engine (M1 skeleton)...");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 通知 Watchdog：已就绪
    mkdir("/tmp/gateway_watchdog", 0755);
    std::ofstream("/tmp/gateway_watchdog/gateway_engine.ready") << "1";

    // M3: 初始化 GStreamer 管线 + RKNN 推理引擎
    // M3: 启动 UDS 客户端连接 gateway_core

    while (g_running) {
        // M3: 主循环：grab frame → infer → publish result
        sleep(1);
    }

    logger->info("Shutting down...");
    return 0;
}
