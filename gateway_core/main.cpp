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
#include "RpcHandler.h"
#include "EventFusion.h"
#include "EngineManager.h"
#include "ConfigManager.h"
#include "HttpDashboard.h"
#include "OtaManager.h"
#include "InternalMessage.h"
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdint>
#include <sstream>

EventLoop* g_event_loop = nullptr;

void signal_handler(int signum) {
    if (g_event_loop) {
        g_event_loop->stop();
    }
}

static bool send_uds_cmd(int fd, int32_t node_id, uint8_t tlv_type, const std::string& payload_str) {
    if (fd < 0) return false;
    InternalMessage msg;
    msg.source_type = 3;
    msg.node_id = node_id;
    msg.tlv_type = tlv_type;
    msg.payload.assign(payload_str.begin(), payload_str.end());
    auto encoded = encode_internal_msg(msg);
    ssize_t n = ::write(fd, encoded.data(), encoded.size());
    return n == (ssize_t)encoded.size();
}

static std::string extract_json_str(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    auto start = json.find('"', pos + key.size() + 2) + 1;
    auto end = json.find('"', start);
    if (start == std::string::npos || end == std::string::npos) return "";
    return json.substr(start, end - start);
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

    std::atomic<float> temp_max{90.0f};
    std::atomic<float> hum_max{80.0f};

    int engine_cmd_fd = -1;
    int access_cmd_fd = -1;

    RuleEngine rule_engine;

    rule_engine.add_rule([&](const InternalMessage& msg){
        std::string data(msg.payload.begin(), msg.payload.end());
        float temp = 0.0f;
        if (data.find("temp=") != std::string::npos) {
            auto pos = data.find("temp=") + 5;
            temp = std::stof(data.substr(pos));
        }
        return temp > temp_max.load();
    },
    [&](const InternalMessage& msg){
        logger->warn("高温告警! node={}", msg.node_id);
    });

    rule_engine.add_rule([&](const InternalMessage& msg) {
        std::string data(msg.payload.begin(), msg.payload.end());
        float hum = 0.0f;
        if (data.find("hum=") != std::string::npos) {
            auto pos = data.find("hum=") + 5;
            hum = std::stof(data.substr(pos));
        }
        return hum > hum_max.load();
    },
    [&](const InternalMessage& msg){
        logger->warn("高湿度告警! node={}", msg.node_id);
    });

    EventLoop event_loop({uds_path, engine_path});
    event_loop.set_monitor_path(monitor_path);
    g_event_loop = &event_loop;

    DbWriter db_writer(db_path);
    db_writer.start();

    OfflineStore offline_store(db_path);
    bool need_flush = false;

    MqttClient mqtt(config["mqtt"]["broker_url"], config["mqtt"]["client_id"], config["mqtt"]["will_topic"]);

    ConfigManager config_manager("conf/gateway.hot.json");

    config_manager.watch("rule_engine", "temp_max", [&](const std::string& v) {
        try { temp_max.store(std::stof(v)); return "ACK"; }
        catch (...) { return "NACK: temp_max 不是有效数值"; }
    });
    config_manager.watch("rule_engine", "hum_max", [&](const std::string& v) {
        try { hum_max.store(std::stof(v)); return "ACK"; }
        catch (...) { return "NACK: hum_max 不是有效数值"; }
    });

    config_manager.load_persisted();

    OtaManager ota_manager("model", db_path);

    RpcHandler rpc_handler;

    rpc_handler.register_method("get_temp", [&](const std::string& payload){
        std::ostringstream oss;
        oss << "{\"temp_current\":" << temp_max.load()
            << ",\"hum_current\":" << hum_max.load() << "}";
        return oss.str();
    });

    rpc_handler.register_method("config_update", [&](const std::string& payload){
        return config_manager.handle_config_update(payload);
    });

    rpc_handler.register_method("set_alarm_threshold", [&](const std::string& payload){
        try {
            std::string t = extract_json_str(payload, "temp_max");
            std::string h = extract_json_str(payload, "hum_max");
            if (!t.empty()) temp_max.store(std::stof(t));
            if (!h.empty()) hum_max.store(std::stof(h));
            return "ACK";
        } catch (...) {
            return "NACK: invalid params";
        }
    });

    rpc_handler.register_method("set_modbus_interval", [&](const std::string& payload){
        if (!send_uds_cmd(access_cmd_fd, 0, 0x20, payload)) {
            return "NACK: access not connected";
        }
        return "ACK";
    });

    rpc_handler.register_method("add_modbus_device", [&](const std::string& payload){
        if (!send_uds_cmd(access_cmd_fd, 0, 0x21, payload)) {
            return "NACK: access not connected";
        }
        return "ACK";
    });

    rpc_handler.register_method("start_analysis", [&](const std::string& payload){
        if (!send_uds_cmd(engine_cmd_fd, 0, 0x10, payload)) {
            return "NACK: engine not connected";
        }
        return "ACK";
    });

    rpc_handler.register_method("stop_analysis", [&](const std::string& payload){
        if (!send_uds_cmd(engine_cmd_fd, 0, 0x11, payload)) {
            return "NACK: engine not connected";
        }
        return "ACK";
    });

    rpc_handler.register_method("switch_model", [&](const std::string& payload){
        std::string model = extract_json_str(payload, "model");
        if (model.empty()) return "NACK: missing model name";
        if (!send_uds_cmd(engine_cmd_fd, 0, 0x12, model)) {
            return "NACK: engine not connected";
        }
        return "ACK";
    });

    rpc_handler.register_method("trigger_offline_sync", [&](const std::string& payload){
        need_flush = true;
        return "ACK";
    });

    rpc_handler.register_method("get_device_health", [&](const std::string& payload){
        std::ostringstream oss;
        oss << "{"
            << "\"mqtt_connected\":" << (mqtt.is_connected() ? "true" : "false") << ","
            << "\"engine_online\":" << (engine_cmd_fd >= 0 ? "true" : "false") << ","
            << "\"access_online\":" << (access_cmd_fd >= 0 ? "true" : "false") << ","
            << "\"temp_max\":" << temp_max.load() << ","
            << "\"hum_max\":" << hum_max.load()
            << "}";
        return oss.str();
    });

    rpc_handler.register_method("ota_update_model", [&](const std::string& payload){
        return ota_manager.handle_ota_update(payload);
    });

    mqtt.set_status_callback([&](bool connected) {
        if (connected) {
            need_flush = true;
            logger->info("MQTT 已连接，触发离线回传");
        } else {
            logger->warn("MQTT 连接断开");
        }
    });

    mqtt.connect();

    mqtt.subscribe("spBv1.0/edge_gateway/DCMD/gw001");

    event_loop.add_external_fd(mqtt.event_fd(), [&](int fd){
        RpcCommand cmd;
        while (mqtt.try_pop_rpc(cmd)) {
            std::string resp = rpc_handler.dispatch(cmd.payload);
            mqtt.publish(cmd.topic, resp);
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

    EventFusion event_fusion;
    event_fusion.load_rules_from_json("conf/ai_rules.json");

    EngineManager engine_manager;

    engine_manager.set_command_sender([&](int32_t node_id, uint8_t cmd) {
        if (engine_cmd_fd < 0) return false;
        InternalMessage msg;
        msg.source_type = 3;
        msg.node_id = node_id;
        msg.tlv_type = cmd;
        auto encoded = encode_internal_msg(msg);
        ssize_t n = ::write(engine_cmd_fd, encoded.data(), encoded.size());
        return n == (ssize_t)encoded.size();
    });

    ota_manager.set_command_sender([&](int32_t node_id, uint8_t cmd, const std::string& path) {
        return send_uds_cmd(engine_cmd_fd, node_id, cmd, path);
    });

    int rule_periodic_counter = 0;
    int64_t total_packets = 0;

    event_loop.set_data_callback([&](int fd, const InternalMessage& msg){
        if (msg.source_type == 3) {
            engine_cmd_fd = fd;
        } else {
            access_cmd_fd = fd;
        }

        total_packets++;

        if (msg.source_type == 3) {
            if (msg.tlv_type == 0x05) {
                engine_manager.on_heartbeat(msg);
                auto timed_out = engine_manager.check_timeout();
                for (auto node_id : timed_out) {
                    logger->warn("进程 E 心跳超时: node={}", node_id);
                }
            } else if (msg.tlv_type == 0x04) {
                auto fusion_result = event_fusion.evaluate(msg);
                if (fusion_result.has_value()) {
                    int severity;
                    std::memcpy(&severity, fusion_result->payload.data()
                        + fusion_result->payload.size() - 4, 4);
                    logger->warn("复合告警! node={}, severity={}", msg.node_id, severity);

                    DbRecord record;
                    record.type = DbOpType::ALARM;
                    record.source_type = fusion_result->source_type;
                    record.node_id = fusion_result->node_id;
                    record.tlv_type = fusion_result->tlv_type;
                    record.data = std::string(fusion_result->payload.begin(),
                                              fusion_result->payload.end());
                    db_writer.push(record);

                    std::string alarm_topic = "spBv1.0/edge_gateway/DALARM/gw001";
                    if (mqtt.is_connected()) {
                        mqtt.publish(alarm_topic, record.data);
                    } else {
                        offline_store.insert(alarm_topic, record.data);
                    }
                }
            }
            return;
        }

        event_fusion.evaluate(msg);

        DbRecord record;
        record.type = DbOpType::SENSOR;
        record.source_type = msg.source_type;
        record.node_id = msg.node_id;
        record.tlv_type = msg.tlv_type;
        record.data = std::string(msg.payload.begin(), msg.payload.end());
        db_writer.push(record);

        rule_engine.evaluate(msg);

        ShmBlock block{};
        block.total_packets = total_packets;
        shm_publisher.publish(block);

        std::string payload_str(msg.payload.begin(), msg.payload.end());
        std::string data_topic = "spBv1.0/edge_gateway/DDATA/gw001";
        if (mqtt.is_connected()) {
            mqtt.publish(data_topic, payload_str);
        } else {
            offline_store.insert(data_topic, payload_str);
        }

        rule_periodic_counter++;
        if (rule_periodic_counter >= 100) {
            rule_periodic_counter = 0;
            auto timed_out = engine_manager.check_timeout();
            for (auto node_id : timed_out) {
                logger->warn("进程 E 心跳超时(周期检查): node={}", node_id);
            }
        }
    });

    HttpDashboard http_dashboard;
    int http_port = std::stoi(config["http"]["port"].empty() ? "8080" : config["http"]["port"]);
    if (!http_dashboard.start(http_port, 0x47574D4D)) {
        logger->warn("HTTP 仪表盘启动失败，不影响主流程");
    }

    mkdir("/tmp/gateway_watchdog", 0755);
    std::ofstream("/tmp/gateway_watchdog/gateway_core.ready") << "1";

    event_loop.start();

    db_writer.stop();

    return 0;
}
