/**
 * mock_cloud_listener.cpp — MQTT 云端监听验证工具
 *
 * 用途：订阅网关所有上报主题，验证 MQTT 上云链路是否正常。
 * 用于开发调试和集成测试，不依赖网关本身。
 *
 * 编译：
 *   g++ -std=c++17 mock_cloud_listener.cpp -o mock_cloud_listener \
 *       -lpaho-mqttpp3 -lpaho-mqtt3a
 *
 * 用法：
 *   ./mock_cloud_listener [broker_url] [client_id]
 *   默认 broker: tcp://localhost:1883
 *   默认 client_id: cloud_listener_001
 *
 * 订阅主题：
 *   spBv1.0/edge_gateway/DDATA/gw001/#   — 传感器数据
 *   spBv1.0/edge_gateway/DALARM/gw001/#  — 告警
 *   spBv1.0/edge_gateway/DBIRTH/gw001/#  — 设备上线
 *   spBv1.0/edge_gateway/DCMD/gw001/#    — RPC 指令（监听用，不响应）
 */

#include <iostream>
#include <csignal>
#include <string>
#include <mqtt/async_client.h>

static volatile sig_atomic_t g_running = 1;

void signal_handler(int) { g_running = 0; }

class CloudListener : public mqtt::callback {
public:
    void connected(const mqtt::string& cause) override {
        std::cout << "[MQTT] Connected: " << cause << std::endl;
    }

    void connection_lost(const mqtt::string& cause) override {
        std::cerr << "[MQTT] Connection lost: " << cause << std::endl;
    }

    void message_arrived(mqtt::const_message_ptr msg) override {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));

        std::cout << "[" << buf << "] " << msg->get_topic()
                  << " QoS=" << msg->get_qos()
                  << " payload=" << msg->get_payload_str()
                  << std::endl;
    }

    void delivery_complete(mqtt::delivery_token_ptr) override {}
};

int main(int argc, char* argv[]) {
    std::string broker = (argc >= 2) ? argv[1] : "tcp://localhost:1883";
    std::string client_id = (argc >= 3) ? argv[2] : "cloud_listener_001";

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::cout << "=== MQTT 云端监听验证工具 ===" << std::endl;
    std::cout << "Broker: " << broker << std::endl;
    std::cout << "Client: " << client_id << std::endl;
    std::cout << "订阅主题: spBv1.0/edge_gateway/+/gw001/#" << std::endl;
    std::cout << "按 Ctrl+C 退出" << std::endl;

    mqtt::async_client client(broker, client_id);

    CloudListener listener;
    client.set_callback(listener);

    mqtt::connect_options opts;
    opts.set_clean_session(true);
    opts.set_keep_alive_interval(30);

    try {
        client.connect(opts)->wait();
        std::cout << "[OK] 已连接到 Broker" << std::endl;

        // 订阅所有网关主题
        client.subscribe("spBv1.0/edge_gateway/+/gw001/#", 1)->wait();
        std::cout << "[OK] 已订阅网关主题" << std::endl;

        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        client.disconnect()->wait();
        std::cout << "[OK] 已断开连接" << std::endl;
    } catch (const mqtt::exception& e) {
        std::cerr << "[ERROR] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}