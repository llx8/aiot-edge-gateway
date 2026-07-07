#include "EventLoop.h"
#include "DbWriter.h"
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
    std::string uds_path = config["uds"]["data_path"];
    std::string engine_path = config["uds"]["engine_path"];
    std::string db_path = config["sqlite"]["db_path"];

    // 监听两个 UDS：进程A(数据) + 进程E(AI)
    EventLoop event_loop({uds_path, engine_path});
    g_event_loop = &event_loop;

    DbWriter db_writer(db_path);
    db_writer.start();

    ShmPublisher shm_publisher(0x47574D4D);

    int64_t total_packets = 0;

    event_loop.set_data_callback([&](const InternalMessage& msg){
        total_packets++;

        // AI 检测结果（M1 阶段只打日志，M3 完整处理）
        if (msg.source_type == 3) {
            logger->info("AI检测结果（M1未处理）: node={}, tlv_type={}", msg.node_id, msg.tlv_type);
            return;
        }

        // 传感器数据：异步入队
        DbRecord record;
        record.type = DbOpType::SENSOR;
        record.source_type = msg.source_type;
        record.node_id = msg.node_id;
        record.tlv_type = msg.tlv_type;
        record.data = std::string(msg.payload.begin(), msg.payload.end());
        db_writer.push(record);

        ShmBlock block{};
        block.total_packets = total_packets;
        shm_publisher.publish(block);

        logger->info("收到消息: source_type={}, node_id={}, tlv_type={}, payload_len={}", 
            msg.source_type, msg.node_id, msg.tlv_type, msg.payload.size());
    });

    event_loop.start();

    db_writer.stop();

    return 0;
}
