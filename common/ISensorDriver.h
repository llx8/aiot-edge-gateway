#pragma once
#include <functional>
#include "InternalMessage.h"
#include <string_view>

// 纯虚函数提供给南向协议的接口
class ISensorDriver{
public:
    virtual ~ISensorDriver() = default;

    // 启动
    virtual void start() = 0;

    // 停止
    virtual void stop()  = 0;

    // 适配器名字
    virtual std::string_view name() const = 0;

    // 动态修改轮询间隔（默认空实现，子类按需覆写）
    virtual void set_poll_interval(uint16_t interval_ms) {}

    // 数据回调函数
    using DataCallback = std::function<void(const InternalMessage& payload)>;

    // 设置回调函数
    virtual void set_data_callback(DataCallback callback){
        m_on_data = std::move(callback);
    }
protected:
    DataCallback m_on_data;
};