#include "EventFusion.h"
#include "Logger.h"
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <unordered_set>

void EventFusion::add_rule(FusionRule rule) {
    rules_.push_back(std::move(rule));
}

std::optional<InternalMessage> EventFusion::evaluate(const InternalMessage& msg) {
    if (msg.source_type >= 0 && msg.source_type <= 2) {
        if (msg.payload.size() >= sizeof(float)) {
            float value;
            std::memcpy(&value, msg.payload.data(), sizeof(float));
            sensor_cache_[msg.node_id] = {value, std::chrono::steady_clock::now()};
        }
        return std::nullopt;
    }

    if (msg.source_type == 3) {
        if (msg.payload.size() < 4) return std::nullopt;

        constexpr size_t DET_SIZE = 24;

        // 兼容两种格式:
        //   批量格式: [4B num] + [num * DET_SIZE]
        //   单检测格式: 直接就是 [DET_SIZE] 一个 Detection
        int32_t num;
        std::memcpy(&num, msg.payload.data(), 4);

        // 试探性判断：如果不是批量格式（payload_size != 4 + num*DET_SIZE），按单检测处理
        bool is_batch = (msg.payload.size() == 4 + static_cast<size_t>(num) * DET_SIZE);

        // 第一遍扫描：收集本帧中所有检测框的 class_id
        std::unordered_set<int> present_classes;
        auto collect_classes = [&](const uint8_t* data, int32_t count) {
            for (int32_t i = 0; i < count; i++) {
                int class_id;
                std::memcpy(&class_id, data + i * DET_SIZE + 20, 4);
                present_classes.insert(class_id);
            }
        };

        if (is_batch) {
            collect_classes(msg.payload.data() + 4, num);
        } else {
            // 单检测格式：整个 payload 就是一个 Detection
            num = 1;
            collect_classes(msg.payload.data(), 1);
        }

        int max_severity = 0;

        auto process_det = [&](const uint8_t* data, const FusionRule& rule) -> bool {
            int class_id;
            std::memcpy(&class_id, data + 20, 4);
            if (rule.ai_class_id != class_id) return false;

            if (rule.requires_co_class >= 0) {
                if (!present_classes.count(rule.requires_co_class)) return false;
            }
            if (rule.excludes_co_class >= 0) {
                if (present_classes.count(rule.excludes_co_class)) return false;
            }

            auto it = sensor_cache_.find(msg.node_id);
            if (it == sensor_cache_.end()) {
                if (!rule.sensor_condition) return false;
                if (!rule.sensor_condition(0.0f)) return false;
            } else {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - it->second.timestamp).count();
                if (elapsed > SENSOR_CACHE_TTL_SEC) {
                    if (!rule.sensor_condition(0.0f)) return false;
                } else {
                    if (!rule.sensor_condition(it->second.value)) return false;
                }
            }

            if (rule.output_severity > max_severity)
                max_severity = rule.output_severity;
            return true;
        };

        for (int32_t i = 0; i < num; i++) {
            const uint8_t* det_ptr = is_batch ? (msg.payload.data() + 4 + i * DET_SIZE) : msg.payload.data();
            for (const auto& rule : rules_) {
                process_det(det_ptr, rule);
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
            std::string co_str = extract("requires_co_class");
            std::string excl_str = extract("excludes_co_class");

            FusionRule rule;
            rule.ai_class_id = class_id;
            rule.output_severity = severity;
            rule.requires_co_class = co_str.empty() ? -1 : std::stoi(co_str);
            rule.excludes_co_class = excl_str.empty() ? -1 : std::stoi(excl_str);

            if (cond == "true") {
                rule.sensor_condition = [](float) { return true; };
            } else if (cond.find("temp >") != std::string::npos) {
                float threshold = extract_val(cond.substr(cond.find('>') + 1));
                rule.sensor_condition = [threshold](float v) { return v > threshold; };
            } else if (cond.find("temp <") != std::string::npos) {
                float threshold = extract_val(cond.substr(cond.find('<') + 1));
                rule.sensor_condition = [threshold](float v) { return v < threshold; };
            } else if (cond.find("hour") != std::string::npos) {
                // 时间条件：hour < 8 || hour > 18 表示非工作时间。
                // 必须用系统时钟取当前小时，不能用传感器数值 v——
                // 原实现 int h = (int)v 把温度/湿度当小时数（如 42.5°C -> h=42 -> 42>18 恒真），
                // 导致"非工作时间"规则在白天也持续触发。这里忽略 v，改读系统时钟。
                int lo = 0, hi = 24;
                auto lo_pos = cond.find("hour <");
                auto hi_pos = cond.find("hour >");
                if (lo_pos != std::string::npos) {
                    lo = static_cast<int>(extract_val(cond.substr(lo_pos + 6)));
                }
                if (hi_pos != std::string::npos) {
                    hi = static_cast<int>(extract_val(cond.substr(hi_pos + 6)));
                }
                if (cond.find("||") != std::string::npos) {
                    // hour < lo || hour > hi
                    rule.sensor_condition = [lo, hi](float) {
                        time_t now = time(nullptr);
                        int h = localtime(&now)->tm_hour;
                        return h < lo || h > hi;
                    };
                } else {
                    rule.sensor_condition = [lo, hi](float) {
                        time_t now = time(nullptr);
                        int h = localtime(&now)->tm_hour;
                        return h >= lo && h <= hi;
                    };
                }
            } else if (cond.find("motor_running") != std::string::npos) {
                // 布尔条件：motor_running == true
                rule.sensor_condition = [](float v) { return v > 0.5f; };
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
