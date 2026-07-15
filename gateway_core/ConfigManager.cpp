#include "ConfigManager.h"
#include "Logger.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <cerrno>

ConfigManager::ConfigManager(const std::string& config_path)
    : config_path_(config_path)
    , backup_path_(config_path + ".bak")
    , temp_path_(config_path + ".tmp")
{}

void ConfigManager::watch(const std::string& section, const std::string& key, Watcher watcher) {
    watchers_.push_back({section, key, std::move(watcher)});
}

// ── JSON 工具（极简，只提取一层嵌套的结构） ──

// JSON 格式校验：检查括号匹配、字符串、基本结构
bool ConfigManager::validate_json(const std::string& json) {
    auto trim = [](const std::string& s) -> std::string {
        size_t start = 0;
        while (start < s.size() && std::isspace(s[start])) start++;
        size_t end = s.size();
        while (end > start && std::isspace(s[end-1])) end--;
        return s.substr(start, end - start);
    };
    std::string t = trim(json);
    if (t.empty() || t[0] != '{' || t[t.size()-1] != '}') {
        return false;
    }

    int depth = 0;
    bool in_string = false;
    bool escape = false;
    bool expect_key = false;  // 在对象内期望 key

    for (size_t i = 0; i < t.size(); i++) {
        char c = t[i];

        if (escape) { escape = false; continue; }

        if (in_string) {
            if (c == '\\') escape = true;
            else if (c == '"') { in_string = false; }
            continue;
        }

        if (c == '"') {
            in_string = true;
            continue;
        }

        if (c == '{' || c == '[') {
            depth++;
            if (c == '{') expect_key = true;
        } else if (c == '}' || c == ']') {
            depth--;
            expect_key = false;
        }

        if (depth < 0) return false;
    }

    return depth == 0 && !in_string;
}

// 从 {"section":{"key":value,...},...} 中提取值
std::string ConfigManager::extract_value(const std::string& json,
                                          const std::string& section,
                                          const std::string& key) {
    auto sec_start = json.find("\"" + section + "\"");
    if (sec_start == std::string::npos) return "";

    auto colon = json.find(':', sec_start);
    if (colon == std::string::npos) return "";
    auto obj_start = json.find('{', colon);
    if (obj_start == std::string::npos) return "";

    auto sub = json.substr(obj_start, json.find('}', obj_start) - obj_start);
    auto kpos = sub.find("\"" + key + "\"");
    if (kpos == std::string::npos) return "";

    auto vcolon = sub.find(':', kpos);
    if (vcolon == std::string::npos) return "";

    auto vstart = vcolon + 1;
    while (vstart < sub.size() && sub[vstart] == ' ') vstart++;

    if (sub[vstart] == '"') {
        auto vend = sub.find('"', vstart + 1);
        if (vend == std::string::npos) return "";
        return sub.substr(vstart + 1, vend - vstart - 1);
    } else {
        auto vend = sub.find_first_of(",\n\r}", vstart);
        if (vend == std::string::npos) return sub.substr(vstart);
        return sub.substr(vstart, vend - vstart);
    }
}

// ── 原子写 ──

bool ConfigManager::backup_current() {
    std::ifstream src(config_path_, std::ios::binary);
    if (!src.is_open()) {
        return true;
    }
    std::ofstream dst(backup_path_, std::ios::binary);
    if (!dst.is_open()) {
        GetLogger("ConfigManager")->error("备份失败: 无法写入 {}", backup_path_);
        return false;
    }
    dst << src.rdbuf();
    return true;
}

bool ConfigManager::atomic_write(const std::string& content) {
    {
        std::ofstream tmp(temp_path_, std::ios::binary);
        if (!tmp.is_open()) {
            GetLogger("ConfigManager")->error("写入临时文件失败: {}", strerror(errno));
            return false;
        }
        tmp.write(content.data(), content.size());
        tmp.flush();
        if (tmp.fail()) {
            GetLogger("ConfigManager")->error("flush 临时文件失败");
            return false;
        }
    }

    if (std::rename(temp_path_.c_str(), config_path_.c_str()) != 0) {
        GetLogger("ConfigManager")->error("原子替换失败: {}", strerror(errno));
        return false;
    }
    return true;
}

// ── 通知 watcher ──

std::string ConfigManager::notify_all(const std::string& json) {
    for (auto& w : watchers_) {
        std::string value;
        if (w.section.empty() && w.key.empty()) {
            value = json;
        } else {
            value = extract_value(json, w.section, w.key);
            if (value.empty()) continue;
        }
        std::string result = w.watcher(value);
        if (result != "ACK") {
            GetLogger("ConfigManager")->warn("模块 [{}/{}] 重载失败: {}",
                w.section, w.key, result);
        }
    }
    return "ACK";
}

// ── 加载持久化的热配置 ──

bool ConfigManager::load_persisted() {
    std::ifstream f(config_path_);
    if (!f.is_open()) {
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string json = ss.str();

    if (!validate_json(json)) {
        GetLogger("ConfigManager")->warn("上次持久化的配置格式异常，跳过加载");
        return false;
    }
    notify_all(json);
    GetLogger("ConfigManager")->info("已加载上次持久化的热配置");
    return true;
}

// ── RPC 入口 ──

std::string ConfigManager::handle_config_update(const std::string& payload) {
    auto logger = GetLogger("ConfigManager");

    auto params_start = payload.find("\"params\"");
    if (params_start == std::string::npos) {
        return "NACK: missing params";
    }
    auto brace = payload.find('{', params_start);
    if (brace == std::string::npos) {
        return "NACK: invalid params format";
    }

    int depth = 0;
    auto end = brace;
    for (; end < payload.size(); end++) {
        if (payload[end] == '{') depth++;
        else if (payload[end] == '}') { depth--; if (depth == 0) break; }
    }
    if (depth != 0) {
        return "NACK: params JSON 括号不匹配";
    }
    std::string params_json = payload.substr(brace, end - brace + 1);

    if (!validate_json(params_json)) {
        return "NACK: params 不是合法 JSON";
    }

    logger->info("收到配置更新: {}", params_json);

    if (!backup_current()) {
        return "NACK: 备份失败";
    }
    if (!atomic_write(params_json)) {
        return "NACK: 写入失败";
    }

    notify_all(params_json);
    logger->info("配置热加载完成");

    // 计算校验和并返回（std::hash，非安全用途，仅用于 ACK 确认）
    std::hash<std::string> hasher;
    size_t hash = hasher(params_json);
    char hash_str[32];
    snprintf(hash_str, sizeof(hash_str), "%016zx", hash);
    return "ACK: config updated, hash=" + std::string(hash_str);
}
