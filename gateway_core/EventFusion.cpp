#include "EventFusion.h"
#include "Logger.h"
#include <cstring>
#include <fstream>
#include <sstream>

void EventFusion::add_rule(FusionRule rule) {
    rules_.push_back(std::move(rule));
}

std::optional<InternalMessage> EventFusion::evaluate(const InternalMessage& msg) {
    if (msg.source_type >= 0 && msg.source_type <= 2) {
        if (msg.payload.size() >= sizeof(float)) {
            float value;
            std::memcpy(&value, msg.payload.data(), sizeof(float));
            sensor_cache_[msg.node_id] = value;
        }
        return std::nullopt;
    }

    if (msg.source_type == 3) {
        if (msg.payload.size() < 4) return std::nullopt;

        int32_t num;
        std::memcpy(&num, msg.payload.data(), 4);

        constexpr size_t DET_SIZE = 24;
        int max_severity = 0;

        for (int32_t i = 0; i < num; i++) {
            size_t off = 4 + i * DET_SIZE;
            if (off + DET_SIZE > msg.payload.size()) break;

            int class_id;
            std::memcpy(&class_id, msg.payload.data() + off + 20, 4);

            for (const auto& rule : rules_) {
                if (rule.ai_class_id != class_id) continue;

                auto it = sensor_cache_.find(msg.node_id);
                if (it == sensor_cache_.end()) {
                    if (!rule.sensor_condition) continue;
                    if (!rule.sensor_condition(0.0f)) continue;
                } else {
                    if (!rule.sensor_condition(it->second)) continue;
                }

                if (rule.output_severity > max_severity)
                    max_severity = rule.output_severity;
            }
        }

        if (max_severity > 0) {
            InternalMessage out;
            out.source_type = 3;
            out.node_id = msg.node_id;
            out.tlv_type = 0x06;
            out.payload = msg.payload;
            out.payload.resize(out.payload.size() + 4);
            std::memcpy(out.payload.data() + out.payload.size() - 4, &max_severity, 4);
            return out;
        }
    }

    return std::nullopt;
}

static float extract_val(const std::string& s) {
    try {
        return std::stof(s);
    } catch (...) {
        return 0.0f;
    }
}

bool EventFusion::load_rules_from_json(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        GetLogger("EventFusion")->error("cannot open rules file: {}", path);
        return false;
    }

    std::stringstream ss;
    ss << f.rdbuf();
    std::string json = ss.str();

    rules_.clear();

    auto rules_start = json.find("\"rules\"");
    if (rules_start == std::string::npos) return false;

    auto arr_start = json.find('[', rules_start);
    if (arr_start == std::string::npos) return false;

    size_t pos = arr_start + 1;
    while (pos < json.size()) {
        if (json[pos] == ']') break;
        if (json[pos] == '{') {
            auto obj_end = json.find('}', pos);
            if (obj_end == std::string::npos) break;
            std::string obj = json.substr(pos + 1, obj_end - pos - 1);

            auto extract = [&](const std::string& key) -> std::string {
                auto k = obj.find("\"" + key + "\"");
                if (k == std::string::npos) return "";
                auto colon = obj.find(':', k);
                if (colon == std::string::npos) return "";
                auto vstart = colon + 1;
                while (vstart < obj.size() && (obj[vstart] == ' ' || obj[vstart] == '\t')) vstart++;
                if (vstart >= obj.size()) return "";
                if (obj[vstart] == '"') {
                    auto vend = obj.find('"', vstart + 1);
                    return obj.substr(vstart + 1, vend - vstart - 1);
                } else {
                    auto vend = obj.find_first_of(", \t\r\n}", vstart);
                    return obj.substr(vstart, vend - vstart);
                }
            };

            int class_id = std::stoi(extract("ai_class_id"));
            int severity = std::stoi(extract("severity"));
            std::string cond = extract("sensor_condition");

            FusionRule rule;
            rule.ai_class_id = class_id;
            rule.output_severity = severity;

            if (cond == "true") {
                rule.sensor_condition = [](float) { return true; };
            } else if (cond.find("temp >") != std::string::npos) {
                float threshold = extract_val(cond.substr(cond.find('>') + 1));
                rule.sensor_condition = [threshold](float v) { return v > threshold; };
            } else if (cond.find("temp <") != std::string::npos) {
                float threshold = extract_val(cond.substr(cond.find('<') + 1));
                rule.sensor_condition = [threshold](float v) { return v < threshold; };
            } else {
                rule.sensor_condition = [](float) { return true; };
            }

            add_rule(std::move(rule));
            GetLogger("EventFusion")->info("loaded rule: class_id={}, severity={}", class_id, severity);

            pos = obj_end + 1;
            continue;
        }
        pos++;
    }

    GetLogger("EventFusion")->info("loaded {} rules from {}", rules_.size(), path);
    return true;
}
