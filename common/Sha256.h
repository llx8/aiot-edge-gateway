#pragma once

#include <string>
#include <cstdint>

// 计算文件 SHA-256，返回十六进制小写 64 字符串；失败（文件无法打开）返回空串
std::string sha256_file(const std::string& file_path);

// 计算内存缓冲区 SHA-256，返回十六进制小写 64 字符串
std::string sha256_buffer(const uint8_t* data, size_t len);
