#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <functional>

// 纯虚函数提供给南向协议的接口
class ISouthbound{
public:
    virtual ~ISouthbound() = default;

    // 启动
    virtual bool start() = 0;

    // 停止
    virtual void stop()  = 0;

    // 适配器名字
    virtual std::string name() const = 0;

    // 数据回调函数
    using DataCallback = std::function<void(const std::vector<uint8_t>& payload)>;

    // 设置回调函数
    virtual void set_data_callback(DataCallback cb) = 0;
};