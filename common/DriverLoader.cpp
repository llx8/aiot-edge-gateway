#include "DriverLoader.h"
#include <dlfcn.h>
#include "Logger.h"

// 加载 .so 文件
bool DriverLoader::load(const std::string& so_path) {
    handle_ = dlopen(so_path.c_str(), RTLD_LAZY);
    if (!handle_) {
        GetLogger("DriverLoader")->error("Failed to load driver: {}", dlerror());
        return false;
    }
    // 获取工厂函数指针
    factory_ = (DriverFactory)dlsym(handle_, "create_driver");
    if (!factory_) {
        GetLogger("DriverLoader")->error("Failed to find create_driver function");
        dlclose(handle_);
        handle_ = nullptr;
        return false;
    }
    return true;
}

// 从已加载的 .so 创建驱动实例
std::unique_ptr<ISensorDriver> DriverLoader::create() {
    if (!handle_) {
        GetLogger("DriverLoader")->error("Driver not loaded");
        return nullptr;
    }
    // 从已加载的 .so 创建驱动实例
    return std::unique_ptr<ISensorDriver>(factory_());
}

// 析构时自动 dlclose
DriverLoader::~DriverLoader() {
    if (handle_) {
        dlclose(handle_);
        handle_ = nullptr;
    }
}