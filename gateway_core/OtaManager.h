#pragma once

#include <string>
#include <functional>
#include <cstdint>

class OtaManager {
public:
    using CommandSender = std::function<bool(int32_t node_id, uint8_t cmd, const std::string& payload)>;
    using StatusReporter = std::function<void(const std::string& status)>;

    OtaManager(const std::string& model_dir, const std::string& db_path);

    void set_command_sender(CommandSender sender);
    void set_status_reporter(StatusReporter reporter);

    std::string handle_ota_update(const std::string& payload);

private:
    bool download_model(const std::string& url, const std::string& dest_path);
    bool verify_sha256(const std::string& file_path, const std::string& expected_hash);
    bool notify_engine_switch(const std::string& model_path);
    void save_version(const std::string& model_name, const std::string& version, const std::string& md5);

    std::string model_dir_;
    std::string db_path_;
    CommandSender command_sender_;
    StatusReporter status_reporter_;
    static constexpr int kChunkSize = 65536;
};
