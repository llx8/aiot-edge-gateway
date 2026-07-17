#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <functional>
#include <mutex>
#include <queue>
#include <mqtt/async_client.h>

// RPC指令（MQTT回调存入， 主线程消费）
struct RpcCommand {
    std::string topic;
    std::string payload;
};

class MqttClient : public mqtt::callback
                 , public mqtt::iaction_listener{
public:
    // 业务回调
    using StatusCallback = std::function<void (bool connected)>;
    // 构造函数
    MqttClient(const std::string& broker_url, const std::string& client_id, const std::string& will_topic);
    // 析构函数
    ~MqttClient();

    // 连接
    bool connect();
    // 断开连接
    void disconnect();
    // 发布消息
    bool publish(const std::string& topic, const std::string& payload, int qos = 1);
    // 订阅主题
    bool subscribe(const std::string& topic, int qos = 1);
    // 查询连接状态
    bool is_connected() const;
    // 连接成功后发布 BIRTH 消息（需在主线程调用）
    bool publish_birth_if_needed();
    // 重连后重新订阅全部 topic（需在主线程调用，由 connected callback 触发）
    void resubscribe_all();
    // 设置状态回调
    void set_status_callback(StatusCallback callback);
    // 取一条待处理的RPC指令
    bool try_pop_rpc(RpcCommand& cmd);
    // eventfd 供epoll监听
    int event_fd() const {
        return event_fd_;
    }
private:
    // callback 接口
    void connected(const mqtt::string& cause) override;
    void connection_lost(const mqtt::string& cause) override;
    void message_arrived(mqtt::const_message_ptr msg) override;
    void delivery_complete(mqtt::delivery_token_ptr token) override;
    // mqtt::iaction_listener 接口
    void on_failure(const mqtt::token& token) override;
    void on_success(const mqtt::token& token) override;
    // 写eventfd唤醒主线程处理RPC指令
    void notify_main_thread();

    struct Subscription {
        std::string topic;
        int qos;
    };

    std::unique_ptr<mqtt::async_client> client_;
    std::string broker_url_;
    std::string will_topic_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> birth_pending_{false};    // BIRTH 消息待发布
    std::atomic<bool> needs_resubscribe_{false}; // 重连后需重新订阅
    int event_fd_;
    // RPC指令队列：mqtt回调入队， 主线程出队
    std::mutex rpc_mutex_;
    std::queue<RpcCommand> rpc_queue_;
    // 已订阅 topic 列表：主线程 subscribe() 写入，主线程 resubscribe_all() 重放
    std::vector<Subscription> subscriptions_;

    StatusCallback status_callback_;
};
