#pragma once

#include <string>
#include <unordered_map>

// 读取 key=value 格式的配置文件
// 文件不存在或解析错误时返回空 map，调用方使用默认值
std::unordered_map<std::string, std::string> load_config(const std::string& filepath);
