#include "Logger.h"
#include <csignal>
#include <cstring>
#include <string>
#include "InternalMessage.h"
#include <unistd.h>
#include <vector>
#include <thread>
#include "Config.h"
#include "ISensorDriver.h"
#include "UdsClient.h"
#include "DriverLoader.h"
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <poll.h>

static volatile sig_atomic_t g_running = 1;

void signal_handler(int signum) {
    g_running = 0;
}

static void rpc_receive_thread(UdsClient& uds) {
    auto logger = GetLogger("gateway_access_rpc");
    while (g_running) {
        struct pollfd pfd;
        pfd.fd = uds.fd();
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 500);
        if (ret <= 0) continue;

        uint8_t buf[4096];
        ssize_t n = read(uds.fd(), buf, sizeof(buf));
        if (n <= 0) continue;

        auto result = decode_internal_msg(buf, n);
        if (!result.ok) continue;

        logger->info("received cmd: tlv_type=0x{:02x}", result.msg.tlv_type);
    }
}

int main(){
    auto logger = GetLogger("gateway_access");
    logger->info("Starting gateway access ....");      

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    auto config = load_config("conf/gateway.conf");
    std::string uds_path = config["uds"]["data_path"];

    logger->info("Config: uds_path={}", uds_path);

    UdsClient uds(uds_path);
    if (!uds.is_connected()) {
        logger->error("UDS connect failed");
        return 1;
    }

    std::vector<std::unique_ptr<ISensorDriver>> drivers;

    auto plugin_dir = config["plugins"]["dir"];
    if (plugin_dir.empty()) plugin_dir = "build/gateway_access/";

    std::error_code ec;
    if (std::filesystem::exists(plugin_dir, ec) && std::filesystem::is_directory(plugin_dir, ec)) {
        for (auto& entry : std::filesystem::directory_iterator(plugin_dir, ec)) {
            if (ec) break;
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
    } else {
        logger->warn("plugin dir not found: {}, using built-in drivers", plugin_dir);
    }

    if (drivers.empty()) {
        logger->info("no .so plugins found, nothing to start");
    }

    for (auto& driver : drivers) {
        logger->info("Starting driver: {}", driver->name());
        driver->start();
    }

    std::thread rpc_thread(rpc_receive_thread, std::ref(uds));
    rpc_thread.detach();

    mkdir("/tmp/gateway_watchdog", 0755);
    std::ofstream("/tmp/gateway_watchdog/gateway_access.ready") << "1";

    while (g_running) {
        pause();
    }

    logger->info("Shutting down...");
    for (auto& driver : drivers) {
        driver->stop();
    }

    return 0;
}
