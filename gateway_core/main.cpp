#include "RuleEngine.h"
#include "EventLoop.h"
#include "DbWriter.h"
#include "ShmPublisher.h"
#include "Config.h"
#include "Logger.h"
#include "ShmLayout.h"
#include <csignal>
#include <sys/signalfd.h>
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
#include <cstring>
#include <sstream>

EventLoop* g_event_loop = nullptr;

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

    // 传感器离线检测：当同节点上次心跳距今超过 30s 时触发
    std::unordered_map<int32_t, time_t> node_last_seen;
    rule_engine.add_rule([&](const InternalMessage& msg) -> bool {
        time_t now = time(nullptr);
        auto it = node_last_seen.find(msg.node_id);
        bool offline = false;
        if (it != node_last_seen.end()) {
            offline = (now - it->second) > 30;  // 先用旧值判断是否离线
        }
        node_last_seen[msg.node_id] = now;  // 再更新最后心跳时间
        return offline;
    },
    [&](const InternalMessage& msg){
        logger->warn("传感器离线告警: node={}", msg.node_id);
    });

    EventLoop event_loop({uds_path, engine_path});
    event_loop.set_monitor_path(monitor_path);
    g_event_loop = &event_loop;

    // 使用 signalfd 替代传统 signal()，纳入 epoll 统一管理
    // 必须在 event_loop 构造之后、子线程（DB/MQTT）启动之前注册
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    int sig_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sig_fd >= 0) {
        event_loop.add_external_fd(sig_fd, [&](int fd) {
            struct signalfd_siginfo siginfo;
            ssize_t n = read(fd, &siginfo, sizeof(siginfo));
            if (n == sizeof(siginfo)) {
                logger->info("收到信号 {}, 优雅退出", siginfo.ssi_signo);
                event_loop.stop();
            }
        });
    } else {
        // 回退到传统 signal（兼容旧内核）
        signal(SIGINT, [](int){ if (g_event_loop) g_event_loop->stop(); });
        signal(SIGTERM, [](int){ if (g_event_loop) g_event_loop->stop(); });
    }

    DbWriter db_writer(db_path);
    db_writer.start();

    OfflineStore offline_store(db_path);
    // 跨线程访问：MQTT 回调线程写 true，主线程 eventfd 回调读+写 false，须原子
    std::atomic<bool> need_flush{false};

    MqttClient mqtt(config["mqtt"]["broker_url"], config["mqtt"]["client_id"],
                "spBv1.0/edge_gateway/DBIRTH/gw001");

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

    OtaManager ota_manager("models", db_path);

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
            if (!t.empty()) {
                float v = std::stof(t);
                if (v < -50.0f || v > 150.0f) return "NACK: temp_max 超出范围 [-50, 150]";
                temp_max.store(v);
            }
            if (!h.empty()) {
                float v = std::stof(h);
                if (v < 0.0f || v > 100.0f) return "NACK: hum_max 超出范围 [0, 100]";
                hum_max.store(v);
            }
            return "ACK";
        } catch (...) {
            return "NACK: invalid params";
        }
    });

    rpc_handler.register_method("set_modbus_interval", [&](const std::string& payload){
        try {
            std::string interval_str = extract_json_str(payload, "interval_ms");
            if (!interval_str.empty()) {
                int interval = std::stoi(interval_str);
                if (interval < 100 || interval > 60000)
                    return "NACK: interval_ms 超出范围 [100, 60000]";
            }
        } catch (...) { return "NACK: invalid interval_ms"; }
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
        // MQTT params 是 JSON；引擎按 design:207 的 "camera=X model=Y" 格式解析
        std::string camera = extract_json_str(payload, "camera");
        std::string model = extract_json_str(payload, "model");
        // 裸模型名补全为 models/ 路径（引擎按路径加载，与 OTA 一致）
        if (!model.empty() && model.find('/') == std::string::npos) {
            model = "models/" + model;
        }
        std::string uds_payload;
        if (!camera.empty()) uds_payload += "camera=" + camera + " ";
        if (!model.empty())  uds_payload += "model=" + model;
        // 两者都为空时引擎用默认模型
        if (!send_uds_cmd(engine_cmd_fd, 0, 0x10, uds_payload)) {
            return "NACK: engine not connected";
        }
        return "ACK";
    });

    rpc_handler.register_method("stop_analysis", [&](const std::string& payload){
        // design:211: "STOP_ANALYSIS camera=zone_A"
        std::string camera = extract_json_str(payload, "camera");
        std::string uds_payload = camera.empty() ? std::string() : ("camera=" + camera);
        if (!send_uds_cmd(engine_cmd_fd, 0, 0x11, uds_payload)) {
            return "NACK: engine not connected";
        }
        return "ACK";
    });

    rpc_handler.register_method("switch_model", [&](const std::string& payload){
        std::string model = extract_json_str(payload, "model");
        if (model.empty()) return "NACK: missing model name";
        // 裸模型名补全为 models/ 路径（引擎按路径加载）
        if (model.find('/') == std::string::npos) {
            model = "models/" + model;
        }
        if (!send_uds_cmd(engine_cmd_fd, 0, 0x12, model)) {
            return "NACK: engine not connected";
        }
        return "ACK";
    });

    rpc_handler.register_method("trigger_offline_sync", [&](const std::string& payload){
        need_flush = true;
        return "ACK";
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
        mqtt.publish_birth_if_needed();
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

    ota_manager.set_status_reporter([&](const std::string& status) {
        logger->warn("OTA 告警: {}", status);
        if (mqtt.is_connected()) {
            mqtt.publish("spBv1.0/edge_gateway/DALARM/gw001", status);
        }
    });

    int rule_periodic_counter = 0;
    int64_t total_packets = 0;

    // 持久共享内存状态：ShmBlock 是复合状态对象，publish() 整块替换共享内存，
    // 故必须维护持久副本增量更新各字段；若每次事件都 new 一个零初始化块再 publish，
    // 后到的发布会把前面事件设的字段清零（如传感器数据 publish 会清掉 ai_engine_online）。
    ShmBlock shm_block{};

    rpc_handler.register_method("get_device_health", [&](const std::string& payload){
        // 读取 CPU 使用率（/proc/stat 第一行）
        float cpu_usage = -1.0f;
        {
            std::ifstream stat("/proc/stat");
            std::string line;
            if (std::getline(stat, line) && line.find("cpu  ") == 0) {
                std::istringstream iss(line.substr(5));
                long user, nice, sys, idle, iowait, irq, softirq, steal;
                if (iss >> user >> nice >> sys >> idle >> iowait >> irq >> softirq >> steal) {
                    long total = user + nice + sys + idle + iowait + irq + softirq + steal;
                    long total_idle = idle + iowait;
                    if (total > 0) cpu_usage = 100.0f * (total - total_idle) / total;
                }
            }
        }

        // 读取内存使用率
        float mem_usage = -1.0f;
        {
            std::ifstream meminfo("/proc/meminfo");
            std::string line;
            long total_kb = 0, avail_kb = 0;
            while (std::getline(meminfo, line)) {
                if (line.find("MemTotal:") == 0) {
                    std::istringstream iss(line.substr(9));
                    iss >> total_kb;
                } else if (line.find("MemAvailable:") == 0) {
                    std::istringstream iss(line.substr(13));
                    iss >> avail_kb;
                }
                if (total_kb > 0 && avail_kb > 0) break;
            }
            if (total_kb > 0) mem_usage = 100.0f * (total_kb - avail_kb) / total_kb;
        }

        int engine_seconds = engine_manager.seconds_since_last_heartbeat(0);

        std::ostringstream oss;
        oss << "{"
            << "\"mqtt_connected\":" << (mqtt.is_connected() ? "true" : "false") << ","
            << "\"engine_online\":" << (engine_cmd_fd >= 0 ? "true" : "false") << ","
            << "\"access_online\":" << (access_cmd_fd >= 0 ? "true" : "false") << ","
            << "\"temp_max\":" << temp_max.load() << ","
            << "\"hum_max\":" << hum_max.load() << ","
            << "\"cpu_usage\":" << cpu_usage << ","
            << "\"mem_usage\":" << mem_usage << ","
            << "\"engine_last_heartbeat_sec\":" << engine_seconds << ","
            << "\"total_packets\":" << total_packets
            << "}";
        return oss.str();
    });

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
                // 同步 AI 指标到共享内存
                if (msg.payload.size() >= 8) {
                    shm_block.total_packets = total_packets;
                    shm_block.ai_engine_online = 1;
                    std::memcpy(&shm_block.inference_fps, msg.payload.data(), 4);
                    std::memcpy(&shm_block.npu_temp_c, msg.payload.data() + 4, 4);
                    shm_publisher.publish(shm_block);
                }
            } else if (msg.tlv_type == 0x07) {
                // JPEG 快照：直接转发给 monitor 进程
                auto encoded = encode_internal_msg(msg);
                event_loop.send_to_monitor(encoded.data(), encoded.size());
                shm_block.total_packets = total_packets;
                shm_block.snapshot_jpeg_len = static_cast<int32_t>(msg.payload.size());
                shm_block.last_detection_ts = static_cast<uint32_t>(time(nullptr));
                shm_publisher.publish(shm_block);
            } else if (msg.tlv_type == 0x04) {
                auto fusion_result = event_fusion.evaluate(msg);
                if (fusion_result.has_value()) {
                    int severity;
                    std::memcpy(&severity, fusion_result->payload.data()
                        + fusion_result->payload.size() - 4, 4);
                    logger->warn("复合告警! node={}, severity={}", msg.node_id, severity);

                    // GPIO 硬件联动（无物理外设，跳过）
                    if (severity >= 3) {
                        logger->info("HIGH 级别告警: severity={}", severity);
                    }
                    if (severity >= 4) {
                        logger->info("CRITICAL 级别告警: severity={}", severity);
                    }

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
                        offline_store.insert(alarm_topic, record.data, 1);  // 告警优先
                    }

                    // 设计:291 多源复合告警 -> SQLite + MQTT + 共享内存
                    shm_block.total_packets = total_packets;
                    shm_block.total_alarms++;
                    shm_block.alarm_active = 1;
                    shm_block.last_detection_ts = static_cast<uint32_t>(time(nullptr));
                    std::string alarm_desc = "AI复合告警 node=" + std::to_string(msg.node_id)
                        + " sev=" + std::to_string(severity);
                    std::strncpy(shm_block.last_alarm, alarm_desc.c_str(),
                                 sizeof(shm_block.last_alarm));
                    shm_block.last_alarm[sizeof(shm_block.last_alarm) - 1] = '\0';
                    shm_publisher.publish(shm_block);
                }
            } else if (msg.tlv_type == 0x13) {
                // 模型切换 ACK：引擎已成功加载新模型（设计:366）
                logger->info("引擎模型切换 ACK (node={})", msg.node_id);
                ota_manager.on_model_switch_ack(msg.node_id);
            } else if (msg.tlv_type == 0x14) {
                // 模型切换 NACK：引擎已回滚旧模型，发布告警（设计:367）
                std::string reason(msg.payload.begin(), msg.payload.end());
                logger->warn("引擎模型切换 NACK (node={}): {}", msg.node_id, reason);
                ota_manager.on_model_switch_nack(msg.node_id, reason);
                // 额外发布 MQTT 告警
                std::string nack_alarm = "模型切换失败 node=" + std::to_string(msg.node_id)
                    + " reason=" + reason;
                if (mqtt.is_connected()) {
                    mqtt.publish("spBv1.0/edge_gateway/DALARM/gw001", nack_alarm);
                } else {
                    offline_store.insert("spBv1.0/edge_gateway/DALARM/gw001", nack_alarm, 1);
                }
            } else if (msg.tlv_type == 0xFE) {
                // FATAL 错误上报：引擎新旧模型均加载失败等致命错误（设计:218,378）
                // 原实现命中末尾 return 被静默丢弃，FATAL 级错误完全被忽略
                std::string reason(msg.payload.begin(), msg.payload.end());
                logger->error("引擎 FATAL (node={}): {}", msg.node_id, reason);
                DbRecord record;
                record.type = DbOpType::ALARM;
                record.source_type = msg.source_type;
                record.node_id = msg.node_id;
                record.tlv_type = msg.tlv_type;
                record.data = reason;
                db_writer.push(record);
                std::string fatal_alarm = "引擎FATAL node=" + std::to_string(msg.node_id)
                    + " reason=" + reason;
                if (mqtt.is_connected()) {
                    mqtt.publish("spBv1.0/edge_gateway/DALARM/gw001", fatal_alarm);
                } else {
                    offline_store.insert("spBv1.0/edge_gateway/DALARM/gw001", fatal_alarm, 1);
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

        shm_block.total_packets = total_packets;
        shm_block.mqtt_connected = mqtt.is_connected() ? 1 : 0;
        shm_publisher.publish(shm_block);

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
