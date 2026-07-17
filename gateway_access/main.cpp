#include "Logger.h"
#include <csignal>
#include <cstring>
#include <string>
#include "InternalMessage.h"
#include <unistd.h>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include "Config.h"
#include "ISensorDriver.h"
#include "UdsClient.h"
#include "DriverLoader.h"
#include "ModbusRtuDriver.h"
#include "ModbusTcpDriver.h"
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <poll.h>

static volatile sig_atomic_t g_running = 1;

void signal_handler(int signum) {
    g_running = 0;
}

// ── eventfd 桥接：消息队列 ──
// 设计:103 — Modbus 线程 push 队列 + write(eventfd)，主 epoll 线程 pop + UDS 转发
struct PendingMsg {
    std::vector<uint8_t> encoded;
};
static std::mutex g_msg_mutex;
static std::queue<PendingMsg> g_msg_queue;
// 用一个独立的 eventfd 作为全局唤醒信号（不绑定到具体 driver）
static int g_wakeup_efd = -1;

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

int main(){
    auto logger = GetLogger("gateway_access");
    logger->info("Starting gateway access ....");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    auto config = load_config("conf/gateway.conf");
    std::string uds_path = config["uds"]["data_path"];

    logger->info("Config: uds_path={}", uds_path);

    // 先通知 watchdog 进程已启动（UDS 连接可能因 core 未就绪而阻塞，
    // 若不先写 ready，watchdog 10s 超时后会 SIGKILL 此进程）
    mkdir("/tmp/gateway_watchdog", 0755);
    std::ofstream("/tmp/gateway_watchdog/gateway_access.ready") << "1";

    UdsClient uds(uds_path, 3);
    if (!uds.is_connected()) {
        logger->error("UDS connect failed");
        return 1;
    }

    // 创建全局唤醒 eventfd（设计:103 桥接核心）
    g_wakeup_efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (g_wakeup_efd < 0) {
        logger->error("eventfd create failed: {}", strerror(errno));
        return 1;
    }

    std::vector<std::unique_ptr<ISensorDriver>> drivers;
    std::mutex drivers_mutex;

    // 数据回调工厂：Modbus 线程 push 队列 + write(eventfd) → 主线程 epoll 消费后写 UDS
    // （不再在 Modbus 线程中直接写 UDS，避免 UDS 满阻塞采集）
    auto make_data_callback = [](int efd) {
        return [efd](const InternalMessage& msg) {
            PendingMsg pm;
            pm.encoded = encode_internal_msg(msg);
            {
                std::lock_guard<std::mutex> lock(g_msg_mutex);
                g_msg_queue.push(std::move(pm));
            }
            uint64_t cnt = 1;
            ::write(efd, &cnt, sizeof(cnt));
        };
    };

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
            driver->set_data_callback(make_data_callback(g_wakeup_efd));
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

    // ── epoll 主循环 ──
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        logger->error("epoll_create1 failed: {}", strerror(errno));
        return 1;
    }

    // 注册 signalfd (替代 signal handler 实现优雅退出)
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    int sigfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);

    {
        struct epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = sigfd;
        if (sigfd >= 0) epoll_ctl(epfd, EPOLL_CTL_ADD, sigfd, &ev);
    }

    // 注册全局 wakeup eventfd
    {
        struct epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = g_wakeup_efd;
        epoll_ctl(epfd, EPOLL_CTL_ADD, g_wakeup_efd, &ev);
    }

    // 注册 UDS fd（读方向：接收进程 B 的 RPC 指令）
    {
        struct epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = uds.fd();
        epoll_ctl(epfd, EPOLL_CTL_ADD, uds.fd(), &ev);
    }

    logger->info("Entering epoll loop (eventfd bridge active)");

    while (g_running) {
        struct epoll_event events[8];
        int nfds = epoll_wait(epfd, events, 8, 1000);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;

            if (fd == sigfd) {
                // 信号到达 → 优雅退出
                struct signalfd_siginfo siginfo;
                ssize_t rn = read(sigfd, &siginfo, sizeof(siginfo));
                (void)rn;
                g_running = 0;
                break;
            }

            if (fd == g_wakeup_efd) {
                // Modbus 线程有数据就绪 → 主线程转发到 UDS
                uint64_t val;
                read(g_wakeup_efd, &val, sizeof(val));  // 消费 eventfd 计数

                // 批量消费队列，一次性写入 UDS
                std::vector<PendingMsg> batch;
                {
                    std::lock_guard<std::mutex> lock(g_msg_mutex);
                    while (!g_msg_queue.empty()) {
                        batch.push_back(std::move(g_msg_queue.front()));
                        g_msg_queue.pop();
                    }
                }
                for (auto& pm : batch) {
                    uds.write(pm.encoded.data(), pm.encoded.size());
                }
                continue;
            }

            if (fd == uds.fd()) {
                // UDS 可读：处理进程 B 的 RPC 指令
                uint8_t buf[4096];
                ssize_t n = read(uds.fd(), buf, sizeof(buf));
                if (n <= 0) continue;

                auto result = decode_internal_msg(buf, n);
                if (!result.ok) continue;

                logger->info("received cmd: tlv_type=0x{:02x}", result.msg.tlv_type);

                std::string payload_str(result.msg.payload.begin(), result.msg.payload.end());

                if (result.msg.tlv_type == 0x20) {
                    // set_modbus_interval
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
                    // add_modbus_device
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

                    // 使用 eventfd 桥接回调（不直接在 Modbus 线程写 UDS）
                    new_driver->set_data_callback(make_data_callback(g_wakeup_efd));
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
    }

    logger->info("Shutting down...");
    for (auto& driver : drivers) {
        driver->stop();
    }

    if (g_wakeup_efd >= 0) close(g_wakeup_efd);
    if (sigfd >= 0) close(sigfd);
    close(epfd);

    return 0;
}
