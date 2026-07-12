#include "EventFusion.h"
#include <cstring>

void EventFusion::add_rule(FusionRule rule) {
    rules_.push_back(std::move(rule));
}

std::optional<InternalMessage> EventFusion::evaluate(const InternalMessage& msg) {
    // 1. 传感器消息 → 更新缓存
    if (msg.source_type >= 0 && msg.source_type <= 2) {
        if (msg.payload.size() >= sizeof(float)) {
            float value;
            std::memcpy(&value, msg.payload.data(), sizeof(float));
            sensor_cache_[msg.node_id] = value;
        }
        return std::nullopt;
    }

    // 2. AI 检测消息 → 匹配融合规则
    if (msg.source_type == 3) {
        if (msg.payload.size() < 4) return std::nullopt;

        int32_t num;
        std::memcpy(&num, msg.payload.data(), 4);

        constexpr size_t DET_SIZE = 24;  // 6 * sizeof(float)
        int max_severity = 0;

        for (int32_t i = 0; i < num; i++) {
            size_t off = 4 + i * DET_SIZE;
            if (off + DET_SIZE > msg.payload.size()) break;

            // class_id 在每个 Detection 的第 6 个字段（偏移 20）
            int class_id;
            std::memcpy(&class_id, msg.payload.data() + off + 20, 4);

            for (const auto& rule : rules_) {
                if (rule.ai_class_id != class_id) continue;

                // 查传感器缓存
                auto it = sensor_cache_.find(msg.node_id);
                if (it == sensor_cache_.end()) continue;
                if (!rule.sensor_condition(it->second)) continue;

                if (rule.output_severity > max_severity)
                    max_severity = rule.output_severity;
            }
        }

        if (max_severity > 0) {
            InternalMessage out;
            out.source_type = 3;
            out.node_id = msg.node_id;
            out.tlv_type = 0x06;  // 复合告警
            out.payload = msg.payload;
            out.payload.resize(out.payload.size() + 4);
            std::memcpy(out.payload.data() + out.payload.size() - 4, &max_severity, 4);
            return out;
        }
    }

    return std::nullopt;
}
