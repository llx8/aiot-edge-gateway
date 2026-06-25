#include "../common/TlvProtocol.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>
#include <thread>
#include <chrono>
#include <iostream>

// 全局配置
static const char* g_host = "127.0.0.1";
static int g_port = 9000;
static int g_count = 5;
static int g_duration = 60;        // 运行时长（秒），0=无限

// 构造心跳包
std::vector<uint8_t> build_heartbeat(int node_id, int seq) {
    Tlvpacket pkt;
    pkt.header.magic = 0x5A5A;
    pkt.header.version = 1;
    pkt.header.type = 0xFF;   // 心跳

    std::string payload = "heartbeat:node_" + std::to_string(node_id) + ":" + std::to_string(seq);
    pkt.value.assign(payload.begin(), payload.end());
    pkt.header.total_len = sizeof(TlvHeader) + pkt.value.size() + sizeof(uint16_t);

    return encode_tlv(pkt);
}

// 构造温湿度包
std::vector<uint8_t> build_sensor(int node_id, float temp, float humidity) {
    Tlvpacket pkt;
    pkt.header.magic = 0x5A5A;
    pkt.header.version = 1;
    pkt.header.type = 0x01;   // 温湿度

    struct Raw { float temp; float humidity; } data{temp, humidity};
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&data);
    pkt.value.assign(raw, raw + sizeof(data));
    pkt.header.total_len = sizeof(TlvHeader) + pkt.value.size() + sizeof(uint16_t);

    return encode_tlv(pkt);
}

// 构造视觉告警包
std::vector<uint8_t> build_alarm(int node_id, const std::string& zone) {
    Tlvpacket pkt;
    pkt.header.magic = 0x5A5A;
    pkt.header.version = 1;
    pkt.header.type = 0x04;   // 视觉告警

    std::string payload = "intrusion:" + zone + ":node_" + std::to_string(node_id);
    pkt.value.assign(payload.begin(), payload.end());
    pkt.header.total_len = sizeof(TlvHeader) + pkt.value.size() + sizeof(uint16_t);

    return encode_tlv(pkt);
}

// 读取指定长度的数据（处理粘包）
bool recv_all(int fd, uint8_t* buf, size_t len) {
    size_t received = 0;
    while (received < len) {
        ssize_t n = recv(fd, buf + received, len - received, 0);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}

// 单节点工作线程
void node_worker(int node_id) {
    // 建立 TCP 连接
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "[node " << node_id << "] socket failed" << std::endl;
        return;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_port);
    inet_pton(AF_INET, g_host, &addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[node " << node_id << "] connect failed: " << strerror(errno) << std::endl;
        close(sock);
        return;
    }
    std::cout << "[node " << node_id << "] connected" << std::endl;

    int seq = 0;
    auto start = std::chrono::steady_clock::now();

    while (true) {
        auto now = std::chrono::steady_clock::now();
        if (g_duration > 0 &&
            std::chrono::duration_cast<std::chrono::seconds>(now - start).count() >= g_duration) {
            break;
        }

        // 心跳：每 2 秒
        auto heartbeat = build_heartbeat(node_id, seq);
        write(sock, heartbeat.data(), heartbeat.size());

        // 每 5 秒额外发一条温湿度
        if (seq % 5 == 0) {
            float temp = 20.0f + static_cast<float>(rand() % 8000) / 100.0f;  // 20~100°C
            float hum  = 30.0f + static_cast<float>(rand() % 6000) / 100.0f;  // 30~90%
            auto sensor = build_sensor(node_id, temp, hum);
            write(sock, sensor.data(), sensor.size());
        }

        // 10% 概率发入侵告警
        if (rand() % 10 == 0) {
            auto alarm = build_alarm(node_id, "zone_" + std::to_string(rand() % 5));
            write(sock, alarm.data(), alarm.size());
        }

        seq++;
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }

    close(sock);
    std::cout << "[node " << node_id << "] finished, seq=" << seq << std::endl;
}

int main(int argc, char* argv[]) {
    srand(time(nullptr));

    // 简单的命令行参数解析
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc)   g_host = argv[++i];
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) g_port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) g_count = atoi(argv[++i]);
        else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) g_duration = atoi(argv[++i]);
    }

    std::cout << "=== Mock Sensor ===" << std::endl;
    std::cout << "target: " << g_host << ":" << g_port << std::endl;
    std::cout << "nodes:  " << g_count << std::endl;
    std::cout << "duration: " << (g_duration > 0 ? std::to_string(g_duration) + "s" : "unlimited") << std::endl;
    std::cout << "==================" << std::endl;

    std::vector<std::thread> threads;
    threads.reserve(g_count);
    for (int i = 0; i < g_count; i++) {
        threads.emplace_back(node_worker, i);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));  // 错开连接建立
    }

    for (auto& t : threads) {
        t.join();
    }

    std::cout << "All nodes finished." << std::endl;
    return 0;
}