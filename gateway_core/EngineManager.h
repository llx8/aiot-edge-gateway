#pragma once

#include "InternalMessage.h"
#include <functional>
#include <unordered_map>
#include <chrono>

class EngineManager {
public:
    EngineManager();

    // 收到心跳消息 (tlv_type=0x05) 时调用
    void on_heartbeat(const InternalMessage& msg);

    // 检查超时，返回已超时的 node_id 列表
    std::vector<int32_t> check_timeout();

    // 发送启停指令的回调（由 main 注入，实际走 UDS 发给进程 E）
    using CommandSender = std::function<void(int32_t node_id, uint8_t cmd)>;
    void set_command_sender(CommandSender sender);

    void start_analysis(int32_t node_id);
    void stop_analysis(int32_t node_id);

    int seconds_since_last_heartbeat(int32_t node_id) const;

private:
    struct HeartbeatInfo {
        std::chrono::steady_clock::time_point last_beat;
        float fps;
        float npu_temp;
    };

    std::unordered_map<int32_t, HeartbeatInfo> heartbeats_;
    CommandSender command_sender_;
    static constexpr int HEARTBEAT_TIMEOUT_MS = 30000;
};
