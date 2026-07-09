#include "MqttClient.h"
#include <sys/eventfd.h>
#include <unistd.h>
#include "Logger.h"

// 构造函数
MqttClient::MqttClient(const std::string& broker_url, const std::string& client_id, const std::string& will_topic) {
    event_fd_ = eventfd(0, EFD_NONBLOCK);
    if (event_fd_ < 0) {
        throw std::runtime_error("eventfd failed");
    }
    client_ = std::make_unique<mqtt::async_client>(broker_url, client_id);
    client_->set_callback(*this);
    will_topic_ = will_topic;
    broker_url_ = broker_url;
}

// 析构函数
MqttClient::~MqttClient() {
    disconnect();
    close(event_fd_);
}

// 查询连接状态
bool MqttClient::is_connected() const{
    if (connected_) {
        return true;
    }
    return false;
}

// 断开连接
void MqttClient::disconnect(){
    if (!connected_) {
        return;
    }
    client_->disconnect()->wait();
    connected_ = false;
}

// 回调函数
void MqttClient::connected(const mqtt::string& cause){
    connected_ = true;
    if (status_callback_) {
        status_callback_(true);
    }
    notify_main_thread();
}

void MqttClient::connection_lost(const mqtt::string& cause){
    connected_ = false;
    if (status_callback_) {
        status_callback_(false);
    }
    notify_main_thread();
}

void MqttClient::message_arrived(mqtt::const_message_ptr msg){
    std::lock_guard<std::mutex> lock(rpc_mutex_);
    rpc_queue_.push({msg->get_topic(), msg->get_payload_str()});
    notify_main_thread();
}

void MqttClient::delivery_complete(mqtt::delivery_token_ptr token){
    // 打印日志
    GetLogger("MqttClient")->info("delivery_complete: {}", token->get_message_id());
}

void MqttClient::on_failure(const mqtt::token& token){
    // 打印日志
    GetLogger("MqttClient")->error("on_failure: {}", token.get_message_id());
}

void MqttClient::on_success(const mqtt::token& token){
    // 打印日志
    GetLogger("MqttClient")->info("on_success: {}", token.get_message_id());
}

void MqttClient::notify_main_thread(){
    uint64_t val = 1;
    write(event_fd_, &val, sizeof(val));
}

void MqttClient::set_status_callback(StatusCallback callback){
    status_callback_ = std::move(callback);
}

bool MqttClient::try_pop_rpc(RpcCommand& cmd){
    std::lock_guard<std::mutex> lock(rpc_mutex_);
    if (rpc_queue_.empty()) {
        return false;
    }
    cmd = rpc_queue_.front();
    rpc_queue_.pop();
    return true;
}

bool MqttClient::publish(const std::string& topic, const std::string& payload, int qos){
    if (!connected_) {
        return false;
    }
    auto msg = mqtt::make_message(topic, payload);
    msg->set_qos(qos);
    client_->publish(msg)->wait();
    return true;
}

// 连接
bool MqttClient::connect(){
    if (connected_) {
        return true;
    }
    mqtt::message will_msg(will_topic_, "offline", 1);
    mqtt::will_options will(will_msg);
    mqtt::connect_options conn_opts;
    conn_opts.set_will(will);

    try {
        client_->connect(conn_opts)->wait();
        return true;
    } catch (const mqtt::exception& e) {
        GetLogger("MqttClient")->error("connect failed: {}", e.what());
        return false;
    }
}

// 订阅主题
bool MqttClient::subscribe(const std::string& topic, int qos){
    if (!connected_) {
        return false;
    }
    client_->subscribe(topic, qos)->wait();
    return true;
}
