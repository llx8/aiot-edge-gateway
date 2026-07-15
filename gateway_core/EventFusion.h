#pragma once
#include "InternalMessage.h"
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>

struct FusionRule {
    int ai_class_id;
    std::function<bool(float)> sensor_condition;
    int output_severity;
    // 跨检测框关联：需要同一帧中同时存在此 class_id（-1=不需要）
    int requires_co_class = -1;
    // 跨检测框关联：同一帧中不能存在此 class_id（-1=不需要）
    int excludes_co_class = -1;
};

class EventFusion {
public:
    std::optional<InternalMessage> evaluate(const InternalMessage& msg);

    void add_rule(FusionRule rule);
    bool load_rules_from_json(const std::string& path);

private:
    struct SensorCacheEntry {
        float value;
        std::chrono::steady_clock::time_point timestamp;
    };
    std::unordered_map<int32_t, SensorCacheEntry> sensor_cache_;
    static constexpr int SENSOR_CACHE_TTL_SEC = 30;  // 传感器数据 30s 窗口
    std::vector<FusionRule> rules_;
};
