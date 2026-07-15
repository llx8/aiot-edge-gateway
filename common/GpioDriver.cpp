#include "GpioDriver.h"
#include "Logger.h"
#include <fstream>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

// 辅助：sysfs 导出 GPIO 引脚
#if defined(__arm__) || defined(__aarch64__)   // ARM 板端：真实 sysfs
static bool export_pin(int pin) {
    std::ofstream ofs("/sys/class/gpio/export");
    if (!ofs) return false;
    ofs << pin;
    return ofs.good();
}

static bool set_direction(int pin, const std::string& dir) {
    std::string path = "/sys/class/gpio/gpio" + std::to_string(pin) + "/direction";
    std::ofstream ofs(path);
    if (!ofs) return false;
    ofs << dir;
    return ofs.good();
}

// 真实硬件实现

bool GpioDriver::set_output(int pin, bool high) {
    // 确保引脚已导出
    std::string pin_path = "/sys/class/gpio/gpio" + std::to_string(pin);
    if (access(pin_path.c_str(), F_OK) != 0) {
        if (!export_pin(pin)) {
            GetLogger("GpioDriver")->error("Failed to export GPIO pin {}", pin);
            return false;
        }
        usleep(100000);  // 等内核创建 sysfs 节点
        if (!set_direction(pin, "out")) {
            GetLogger("GpioDriver")->error("Failed to set direction for GPIO pin {}", pin);
            return false;
        }
    }
    std::string val_path = pin_path + "/value";
    std::ofstream ofs(val_path);
    if (!ofs) return false;
    ofs << (high ? "1" : "0");
    GetLogger("GpioDriver")->info("GPIO{} -> {}", pin, high ? "HIGH" : "LOW");
    return ofs.good();
}

bool GpioDriver::read_input(int pin, bool& value) {
    std::string pin_path = "/sys/class/gpio/gpio" + std::to_string(pin);
    if (access(pin_path.c_str(), F_OK) != 0) {
        if (!export_pin(pin)) return false;
        usleep(100000);
        if (!set_direction(pin, "in")) return false;
    }
    std::string val_path = pin_path + "/value";
    std::ifstream ifs(val_path);
    if (!ifs) return false;
    char ch;
    ifs >> ch;
    value = (ch == '1');
    return true;
}

#else  // PC mock：写临时文件验证行为

#include <cstdlib>

static std::string mock_path(int pin, const char* suffix) {
    std::string dir = "/tmp/gpio_mock";
    mkdir(dir.c_str(), 0755);
    return dir + "/gpio" + std::to_string(pin) + suffix;
}

bool GpioDriver::set_output(int pin, bool high) {
    std::string path = mock_path(pin, ".val");
    std::ofstream ofs(path);
    if (!ofs) {
        GetLogger("GpioDriver")->error("Mock GPIO{} write failed", pin);
        return false;
    }
    ofs << (high ? "1" : "0");
    GetLogger("GpioDriver")->info("GPIO{} -> {} (mock)", pin, high ? "HIGH" : "LOW");
    return true;
}

bool GpioDriver::read_input(int pin, bool& value) {
    std::string path = mock_path(pin, ".val");
    std::ifstream ifs(path);
    if (!ifs) {
        // 首次读取：创建默认状态
        std::ofstream ofs(path);
        if (!ofs) return false;
        ofs << "0";
        ofs.close();
        ifs.open(path);
    }
    char ch;
    ifs >> ch;
    value = (ch == '1');
    return true;
}

#endif
