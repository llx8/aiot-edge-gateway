#pragma once
#include "InternalMessage.h"
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>


// 融合规则：一条规则 = AI类别 + 传感器条件 + 输出严重度
struct FusionRule {
    int ai_class_id; // 触发此规则的AI类别
    std::function<bool(float)>sensor_condition;              // 传感器条件（值→是否匹配）
    int output_severity;                                      // 1=INFO, 2=MEDIUM, 3=HIGH, 4=CRITICAL
};

// EventFusion 类
class EventFusion {
public:
    // 入口：来一条 InternalMessage
    //  - 传感器消息 (source_type 0-2) → 更新缓存，返回 nullopt
    //  - AI 检测消息 (source_type 3)  → 查缓存+匹配规则，返回复合告警或 nullopt
    std::optional<InternalMessage> evaluate(const InternalMessage& msg);

    void add_rule(FusionRule rule);

private:
    std::unordered_map<int32_t /*node_id*/, float> sensor_cache_;
    std::vector<FusionRule> rules_;
};
