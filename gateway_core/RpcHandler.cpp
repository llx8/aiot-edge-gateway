#include "RpcHandler.h"

void RpcHandler::register_method(const std::string& method, MethodHandler handler) {
    handlers_.push_back({method, handler});
}

static std::string extract_json_str(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    auto val_start = json.find('"', pos + key.size() + 2);
    if (val_start == std::string::npos) return "";
    auto start = val_start + 1;
    auto end = json.find('"', start);
    if (end == std::string::npos) return "";
    return json.substr(start, end - start);
}

std::string RpcHandler::dispatch(const std::string& payload) {
    // 从 "method":"xxx" 提取方法名
    auto pos = payload.find("\"method\"");
    if (pos == std::string::npos) return "NACK: missing method";

    auto val_start = payload.find('"', pos + 8);  // 跳过 "method":
    if (val_start == std::string::npos) return "NACK: malformed method field";
    auto start = val_start + 1;
    auto end = payload.find('"', start);
    if (end == std::string::npos) return "NACK: malformed method field";
    std::string method_name = payload.substr(start, end - start);

    // 防重放：提取 id 字段并校验
    std::string req_id = extract_json_str(payload, "id");
    if (!req_id.empty()) {
        std::lock_guard<std::mutex> lock(seen_mutex_);
        if (seen_ids_.count(req_id)) {
            return "NACK: duplicate request id";
        }
        // 防止内存膨胀：FIFO 逐出最旧条目，而非全部清空（全部清空会打开重放窗口）
        if (seen_ids_.size() >= kMaxSeenIds) {
            seen_ids_.erase(id_order_.front());
            id_order_.pop_front();
        }
        seen_ids_.insert(req_id);
        id_order_.push_back(req_id);
    }

    // 匹配注册的 handler
    for (auto& [method, handler] : handlers_) {
        if (method == method_name) {
            return handler(payload);
        }
    }
    return "NACK: unknown method " + method_name;
}
