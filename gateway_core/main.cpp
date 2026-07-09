#include "RuleEngine.h"
#include "EventLoop.h"
#include "DbWriter.h"
#include "ShmPublisher.h"
#include "Config.h"
#include "Logger.h"
#include "ShmLayout.h"
#include <csignal>
#include "MqttClient.h"
#include <mqtt/message.h>
#include "OfflineStore.h"

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
    std::string monitor_path = config["uds"]["monitor_path"];

    // 规则引擎
    RuleEngine rule_engine;
    // 添加规则
    rule_engine.add_rule([&](const InternalMessage& msg){
        // 温度
        std::string data(msg.payload.begin(), msg.payload.end());
        float temp = 0.0f;
        if (data.find("temp=") != std::string::npos) {
            auto pos = data.find("temp=") + 5;
            temp = std::stof(data.substr(pos));
            logger->info("温度值: node={}, temp={}", msg.node_id, temp);
        }
        return temp > 90.0f;
    }, 
    [&](const InternalMessage& msg){
        logger->warn("高温告警! node={}", msg.node_id);
        // 后续扩展...
    });
    rule_engine.add_rule([&](const InternalMessage& msg) {
        // 湿度
        std::string data(msg.payload.begin(), msg.payload.end());
        float hum = 0.0f;
        if (data.find("hum=") != std::string::npos) {
            auto pos = data.find("hum=") + 5;
            hum = std::stof(data.substr(pos));
            logger->info("湿度值: node={}, hum={}", msg.node_id, hum);
        }
        return hum > 80.0f;
    }, 
    [&](const InternalMessage& msg){
        logger->warn("高湿度告警! node={}", msg.node_id);
        // 后续扩展...
    });
    
    // 监听两个 UDS：进程A(数据) + 进程E(AI)
    EventLoop event_loop({uds_path, engine_path});
    event_loop.set_monitor_path(monitor_path);
    g_event_loop = &event_loop;

    DbWriter db_writer(db_path);
    db_writer.start();

    OfflineStore offline_store(db_path);
    bool need_flush = false;

    // Mqtt
    MqttClient mqtt(config["mqtt"]["broker_url"], config["mqtt"]["client_id"], config["mqtt"]["will_topic"]);

    mqtt.set_status_callback([&](bool connected) {
        if (connected) {
            need_flush = true;
            logger->info("MQTT 已连接，触发离线回传");
        } else {
            logger->warn("MQTT 连接断开");
        }
    });

    mqtt.connect();

    mqtt.subscribe("spBv1.0/.../DCMD/gw001");
    event_loop.add_external_fd(mqtt.event_fd(), [&](int fd){
        RpcCommand cmd;
        while (mqtt.try_pop_rpc(cmd)) {
            logger->info("收到RPC指令: topic={}, payload={}", cmd.topic, cmd.payload);
        }
        if (need_flush && mqtt.is_connected()) {
            offline_store.flush(mqtt);
            need_flush = false;
        }
    });

    ShmPublisher shm_publisher(0x47574D4D);

    event_loop.set_fd_received_callback([&](int fd) {
    shm_publisher.set_notify_fd(fd);
    });

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

        // 规则引擎
        rule_engine.evaluate(msg);

        ShmBlock block{};
        block.total_packets = total_packets;
        shm_publisher.publish(block);

        // MQTT 上云 / 离线暂存
        std::string payload_str(msg.payload.begin(), msg.payload.end());
        std::string data_topic = "spBv1.0/gw001/DDATA";
        if (mqtt.is_connected()) {
            mqtt.publish(data_topic, payload_str);
        } else {
            offline_store.insert(data_topic, payload_str);
        }

        logger->info("收到消息: source_type={}, node_id={}, tlv_type={}, payload_len={}", 
            msg.source_type, msg.node_id, msg.tlv_type, msg.payload.size());
    });

    event_loop.start();

    db_writer.stop();

    return 0;
}
