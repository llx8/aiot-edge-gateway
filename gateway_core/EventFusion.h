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
