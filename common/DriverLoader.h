#pragma once
#include "ISensorDriver.h"
#include <memory>
#include <string>

// 工厂函数类型：无参数，返回 ISensorDriver*
using DriverFactory = ISensorDriver* (*)();

class DriverLoader {
public:
    // 加载 .so 文件
    bool load(const std::string& so_path);
    // 从已加载的 .so 创建驱动实例
    std::unique_ptr<ISensorDriver> create();
    // 析构时自动 dlclose
    ~DriverLoader();
private:
    void* handle_ = nullptr;
    DriverFactory factory_ = nullptr;
};