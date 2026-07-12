#pragma once

#include <string>
#include <functional>
#include <vector>
#include <memory>

// 配置热加载管理器
// 职责：MQTT RPC 收到 config_update → 校验/备份/原子写入/模块通知/ACK
class ConfigManager {
public:
    // watcher: section + key → 新值 → 返回 ACK 或 "NACK: 原因"
    using Watcher = std::function<std::string(const std::string& value)>;

    ConfigManager(const std::string& config_path);

    // 注册对某个配置键的变更监听
    // section 和 key 都为空时表示整段配置变更（如 JSON 的二级结构）
    void watch(const std::string& section, const std::string& key, Watcher watcher);

    // RPC 入口：处理 config_update 请求
    std::string handle_config_update(const std::string& payload);

    // 启动时从持久化文件加载热配置
    bool load_persisted();

private:
    std::string config_path_;       // 持久化路径（如 conf/gateway.hot.json）
    std::string backup_path_;       // 备份路径
    std::string temp_path_;         // 临时写入路径

    struct WatchEntry {
        std::string section;
        std::string key;
        Watcher watcher;
    };
    std::vector<WatchEntry> watchers_;

    // JSON 工具函数
    bool validate_json(const std::string& json);
    std::string extract_value(const std::string& json,
                              const std::string& section,
                              const std::string& key);

    // 原子写 + 备份
    bool backup_current();
    bool atomic_write(const std::string& content);

    // 通知所有 watcher
    std::string notify_all(const std::string& json);
};
