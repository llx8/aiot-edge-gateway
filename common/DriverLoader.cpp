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

// 不 dlclose：驱动对象生命周期跨过 DriverLoader，提前卸载会导致
// driver->start() 等后续调用因库被卸载而 segfault。
// 库随进程退出自然释放，或调用者显式 close_driver()。
DriverLoader::~DriverLoader() {
    (void)handle_;
}