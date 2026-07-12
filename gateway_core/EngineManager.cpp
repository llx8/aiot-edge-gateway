#include "EngineManager.h"
#include "Logger.h"
#include <cstring>
#include <algorithm>

EngineManager::EngineManager() {}

void EngineManager::on_heartbeat(const InternalMessage& msg) {
    if (msg.payload.size() < 8) {
        GetLogger("EngineManager")->warn("心跳消息 payload 过短: {}", msg.payload.size());
        return;
    }

    HeartbeatInfo info;
    info.last_beat = std::chrono::steady_clock::now();
    std::memcpy(&info.fps, msg.payload.data(), 4);
    std::memcpy(&info.npu_temp, msg.payload.data() + 4, 4);

    heartbeats_[msg.node_id] = info;

    GetLogger("EngineManager")->info("进程 E 心跳: node={}, fps={:.1f}, npu_temp={:.1f}",
        msg.node_id, info.fps, info.npu_temp);
}

std::vector<int32_t> EngineManager::check_timeout() {
    std::vector<int32_t> timed_out;
    auto now = std::chrono::steady_clock::now();

    for (const auto& [node_id, info] : heartbeats_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - info.last_beat).count();
        if (elapsed > HEARTBEAT_TIMEOUT_MS) {
            timed_out.push_back(node_id);
            GetLogger("EngineManager")->warn("进程 E 心跳超时: node={}, 已过 {}ms", node_id, elapsed);
        }
    }

    return timed_out;
}

void EngineManager::set_command_sender(CommandSender sender) {
    command_sender_ = std::move(sender);
}

void EngineManager::start_analysis(int32_t node_id) {
    if (command_sender_) {
        command_sender_(node_id, CMD_START_ANALYSIS);
        GetLogger("EngineManager")->info("发送 START_ANALYSIS: node={}", node_id);
    }
}

void EngineManager::stop_analysis(int32_t node_id) {
    if (command_sender_) {
        command_sender_(node_id, CMD_STOP_ANALYSIS);
        GetLogger("EngineManager")->info("发送 STOP_ANALYSIS: node={}", node_id);
    }
}

int EngineManager::seconds_since_last_heartbeat(int32_t node_id) const {
    auto it = heartbeats_.find(node_id);
    if (it == heartbeats_.end()) return -1;

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - it->second.last_beat).count();
    return static_cast<int>(elapsed);
}
