#include "Config.h"
#include <fstream>
#include <algorithm>

// 去掉首尾空格
static std::string trim(const std::string& str) {
    size_t start = 0;
    while (start < str.size() && (str[start] == ' ' || str[start] == '\t' || str[start] == '\r')) {
        start++;
    }
    size_t end = str.size();
    while (end > start && (str[end - 1] == ' ' || str[end - 1] == '\t' || str[end - 1] == '\r')) {
        end--;
    }
    return str.substr(start, end - start);
}

ConfigMap load_config(const std::string& filepath) {
    ConfigMap config;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        return config;
    }

    std::string line;
    std::string current_section;
    while (std::getline(file, line)) {
        line = trim(line);

        // 跳过空行和注释行
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // 检查是否是节标题
        if (line[0] == '[' && line[line.size() - 1] == ']') {
            current_section = trim(line.substr(1, line.size() - 2));
            continue;
        }

        // 找 key=value
        size_t pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }

        std::string key = trim(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));

        if (!key.empty() && !value.empty()) {
            config[current_section][key] = value;
        }
    }

    return config;
}
