#pragma once

#include <string>
#include <vector>
#include <functional>

class RpcHandler {
public:
    using MethodHandler = std::function<std::string(const std::string& payload)>;

    // 注册一个 method 对应的处理函数
    void register_method(const std::string& method, MethodHandler handler);

    // 分发 RPC 消息，返回响应字符串（ACK/NACK）
    std::string dispatch(const std::string& payload);

private:
    std::vector<std::pair<std::string, MethodHandler>> handlers_;
};