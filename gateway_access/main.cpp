#include "Logger.h"
#include <csignal>
#include <cstring>
#include <string>
#include "InternalMessage.h"
#include <unistd.h>
#include <vector>
#include <thread>
#include <mutex>
#include "Config.h"
#include "ISensorDriver.h"
#include "UdsClient.h"
#include "DriverLoader.h"
#include "ModbusRtuDriver.h"
#include "ModbusTcpDriver.h"
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <poll.h>

static volatile sig_atomic_t g_running = 1;

void signal_handler(int signum) {
    g_running = 0;
}

static std::string extract_json_str(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    auto start = json.find('"', pos + key.size() + 2) + 1;
    auto end = json.find('"', start);
    if (start == std::string::npos || end == std::string::npos) return "";
    return json.substr(start, end - start);
}

static int extract_json_int(const std::string& json, const std::string& key, int default_val = 0) {
    auto s = extract_json_str(json, key);
    if (s.empty()) return default_val;
    try { return std::stoi(s); } catch (...) { return default_val; }
}

static void rpc_receive_thread(UdsClient& uds,
                               std::vector<std::unique_ptr<ISensorDriver>>& drivers,
                               std::mutex& drivers_mutex) {
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

        std::string payload_str(result.msg.payload.begin(), result.msg.payload.end());

        if (result.msg.tlv_type == 0x20) {
            // set_modbus_interval: {"device_index": 0, "interval_ms": 500}
            int idx = extract_json_int(payload_str, "device_index", -1);
            int interval = extract_json_int(payload_str, "interval_ms", 0);
            if (interval < 50 || interval > 60000) {
                logger->warn("set_modbus_interval: invalid interval {}", interval);
                continue;
            }
            std::lock_guard<std::mutex> lock(drivers_mutex);
            if (idx >= 0 && idx < (int)drivers.size()) {
                drivers[idx]->set_poll_interval(static_cast<uint16_t>(interval));
                logger->info("set_modbus_interval: driver[{}] -> {}ms", idx, interval);
            } else {
                logger->warn("set_modbus_interval: invalid device_index {}", idx);
            }
        } else if (result.msg.tlv_type == 0x21) {
            // add_modbus_device: {"type": "rtu", "port": "/dev/ttyUSB0", "slave_id": 2,
            //                     "interval_ms": 1000, "reg_start": 0, "reg_count": 10}
            std::string dev_type = extract_json_str(payload_str, "type");
            std::string port = extract_json_str(payload_str, "port");
            std::string ip = extract_json_str(payload_str, "ip");
            int slave_id = extract_json_int(payload_str, "slave_id", 1);
            int interval = extract_json_int(payload_str, "interval_ms", 1000);
            int reg_start = extract_json_int(payload_str, "reg_start", 0);
            int reg_count = extract_json_int(payload_str, "reg_count", 10);
            int tcp_port = extract_json_int(payload_str, "tcp_port", 502);

            std::unique_ptr<ISensorDriver> new_driver;
            if (dev_type == "rtu") {
                if (port.empty()) { logger->warn("add_modbus_device: missing port"); continue; }
                new_driver = std::make_unique<ModbusRtuDriver>(
                    port, static_cast<uint8_t>(slave_id),
                    static_cast<uint16_t>(interval),
                    static_cast<uint16_t>(reg_start),
                    static_cast<uint16_t>(reg_count));
            } else if (dev_type == "tcp") {
                if (ip.empty()) { logger->warn("add_modbus_device: missing ip"); continue; }
                new_driver = std::make_unique<ModbusTcpDriver>(
                    ip, static_cast<uint16_t>(tcp_port),
                    static_cast<uint8_t>(slave_id),
                    static_cast<uint16_t>(interval),
                    static_cast<uint16_t>(reg_start),
                    static_cast<uint16_t>(reg_count));
            } else {
                logger->warn("add_modbus_device: unknown type {}", dev_type);
                continue;
            }

            new_driver->set_data_callback([&uds](const InternalMessage& msg) {
                auto encoded = encode_internal_msg(msg);
                uds.write(encoded.data(), encoded.size());
            });
            new_driver->start();

            {
                std::lock_guard<std::mutex> lock(drivers_mutex);
                drivers.push_back(std::move(new_driver));
            }
            logger->info("add_modbus_device: added {} driver, total drivers={}",
                         dev_type, drivers.size());
        }
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
    std::mutex drivers_mutex;

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

    std::thread rpc_thread(rpc_receive_thread, std::ref(uds),
                           std::ref(drivers), std::ref(drivers_mutex));
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
