#pragma once

// GPIO 操作封装，编译期自动切换 真实硬件/mock
// 用途：规则引擎触发报警时控制 LED、继电器等输出设备
class GpioDriver {
public:
    // pin: GPIO 引脚编号（BCM 编号或板级编号，取决于平台）
    // direction: "out" 或 "in"

    // 写输出引脚（高/低电平）
    static bool set_output(int pin, bool high);

    // 读输入引脚
    static bool read_input(int pin, bool& value);
};
