#include "EventLoop.h"
#include "SqliteStore.h"
#include "ShmPublisher.h"
#include "Config.h"
#include "Logger.h"
#include "ShmLayout.h"
#include <csignal>

EventLoop* g_event_loop = nullptr;

void signal_handler(int signum) {
    if (g_event_loop) {
        g_event_loop->stop();
    }
}

int main(){
    auto logger = GetLogger("gateway_core");
    logger->info("Starting gateway core...");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    auto config = load_config("conf/gateway.conf");
    std::string uds_path = config["uds_path"];
    std::string db_path = config["db_path"];

    EventLoop event_loop(uds_path);
    g_event_loop = &event_loop;

    SqliteStore sqlite_store(db_path);
    ShmPublisher shm_publisher(0x47574D4D);

    int64_t total_packets = 0;
    int64_t version = 0;

    event_loop.set_data_callback([&](const InternalMessage& msg){
        total_packets++;
        version++;

        sqlite_store.insert_sensor(msg.header.timestamp_ms, msg.header.src_type, std::string(msg.payload.begin(), msg.payload.end()));

        ShmBlock block{};
        block.magic = SHM_MAGIC;
        block.version = version;
        block.total_packets = total_packets;
        shm_publisher.publish(block);

        logger->info("收到消息: timestamp={}, src_type={}, payload_len={}", 
            msg.header.timestamp_ms, static_cast<int>(msg.header.src_type), msg.header.payload_len);
    });

    event_loop.start();

    return 0;
}