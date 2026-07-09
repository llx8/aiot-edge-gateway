#include "Logger.h"
#include <csignal>
#include <cstring>
#include <string>
#include "InternalMessage.h"
#include <unistd.h>
#include <vector>
#include "Config.h"
#include "ISensorDriver.h"
#include "UdsClient.h"
#include "DriverLoader.h"
#include <filesystem>
#include <fstream>
#include <sys/stat.h>


static volatile sig_atomic_t g_running = 1;

void signal_handler(int signum) {
    g_running = 0;
}

int main(){
    auto logger = GetLogger("gateway_access");
    logger->info("Starting gateway access .... ");      

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    auto config = load_config("conf/gateway.conf");
    std::string uds_path = config["uds"]["data_path"];

    logger->info("Config: uds_path={}", uds_path);

    // 连接进程B
    UdsClient uds(uds_path);
    if (!uds.is_connected()) {
        logger->error("UDS connect failed");
        return 1;
    }

    // 注册驱动：从插件目录加载所有 .so
std::vector<std::unique_ptr<ISensorDriver>> drivers;
auto plugin_dir = config["plugins"]["dir"];  // 如 "/usr/lib/gateway/plugins"
if (plugin_dir.empty()) plugin_dir = "plugins/";  // 默认

for (auto& entry : std::filesystem::directory_iterator(plugin_dir)) {
    if (entry.path().extension() != ".so") continue;
    DriverLoader loader;
    if (!loader.load(entry.path().string())) continue;
    auto driver = loader.create();
    if (!driver) continue;
    driver->set_data_callback([&uds](const InternalMessage& msg) {
        auto encoded = encode_internal_msg(msg);
        uds.write(encoded.data(), encoded.size());
    });
    logger->info("Loaded plugin: {}", driver->name());
    drivers.push_back(std::move(driver));
}

    // 启动所有驱动
    for (auto& driver : drivers) {
        logger->info("Starting driver: {}", driver->name());
        driver->start();
    }

    // 主循环：等信号退出
    // 通知 Watchdog：已就绪
    mkdir("/tmp/gateway_watchdog", 0755);
    std::ofstream("/tmp/gateway_watchdog/gateway_access.ready") << "1";

    while (g_running) {
        pause();
    }

    logger->info("Shutting down...");
    for (auto& driver : drivers) {
        driver->stop();
    }
    // UdsClient 析构自动 close fd

    return 0;
}
