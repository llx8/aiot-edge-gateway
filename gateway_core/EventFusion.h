#pragma once
#include "InternalMessage.h"
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>
#include <string>

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
    std::unordered_map<int32_t, float> sensor_cache_;
    std::vector<FusionRule> rules_;
};
