#pragma once

#include <string>
#include <functional>
#include <cstdint>
#include <optional>
#include <chrono>

class OtaManager {
public:
    using CommandSender = std::function<bool(int32_t node_id, uint8_t cmd, const std::string& payload)>;
    using StatusReporter = std::function<void(const std::string& status)>;

    OtaManager(const std::string& model_dir, const std::string& db_path);

    void set_command_sender(CommandSender sender);
    void set_status_reporter(StatusReporter reporter);

    std::string handle_ota_update(const std::string& payload);

    // 引擎模型切换 ACK/NACK 回调（设计:365-367：B 必须等 E 的 ACK 才记录版本）
    void on_model_switch_ack(int32_t node_id);
    void on_model_switch_nack(int32_t node_id, const std::string& reason);

private:
    bool download_model(const std::string& url, const std::string& dest_path);
    bool verify_sha256(const std::string& file_path, const std::string& expected_hash);
    bool notify_engine_switch(const std::string& model_path, const std::string& sha256);
    void save_version(const std::string& model_name, const std::string& version, const std::string& sha256);

    std::string model_dir_;
    std::string db_path_;
    CommandSender command_sender_;
    StatusReporter status_reporter_;
    static constexpr int kChunkSize = 65536;

    // 异步 ACK 状态机
    struct PendingOta {
        std::string model_name;
        std::string version;
        std::string sha256;
        std::string dest_path;  // 失败时用于清理下载文件
    };
    std::optional<PendingOta> pending_;
};
