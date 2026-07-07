#include "Logger.h"
#include <csignal>
#include <cstring>
#include <string>
#include "InternalMessage.h"
#include <unistd.h>
#include <vector>
#include "Config.h"
#include "ModbusRtuDriver.h"
#include "ISensorDriver.h"
#include "UdsClient.h"

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

    // 注册驱动
    std::vector<std::unique_ptr<ISensorDriver>> drivers;

    // Modbus 驱动
    auto modbus = std::make_unique<ModbusRtuDriver>("/dev/ttyUSB0", 1, 1000, 0, 1000);
    modbus->set_data_callback([&uds](const InternalMessage& msg){
        auto encoded = encode_internal_msg(msg);
        uds.write(encoded.data(), encoded.size());
    });
    drivers.push_back(std::move(modbus));

    // 后续注册其他驱动（M2扩展）
    // auto modbus_tcp = std::make_unique<ModbusTcpDriver>(...);
    // drivers.push_back(std::move(modbus_tcp));

    // 启动所有驱动
    for (auto& driver : drivers) {
        logger->info("Starting driver: {}", driver->name());
        driver->start();
    }

    // 主循环：等信号退出
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
