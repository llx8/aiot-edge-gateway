#include "RpcHandler.h"

void RpcHandler::register_method(const std::string& method, MethodHandler handler) {
    handlers_.push_back({method, handler});
}

std::string RpcHandler::dispatch(const std::string& payload) {
    // 从 "method":"xxx" 提取方法名
    auto pos = payload.find("\"method\"");
    if (pos == std::string::npos) return "NACK: missing method";
    
    auto start = payload.find('"', pos + 8) + 1;  // 跳过 "method":
    auto end = payload.find('"', start);
    std::string method_name = payload.substr(start, end - start);
    
    // 匹配注册的 handler
    for (auto& [method, handler] : handlers_) {
        if (method == method_name) {
            return handler(payload);
        }
    }
    return "NACK: unknown method " + method_name;
}
