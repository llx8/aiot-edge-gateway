#pragma once

#include "pipeline/Pipeline.h"
#include "UdsClient.h"
#include <chrono>

class HeartbeatReporter {
public:
    HeartbeatReporter(gateway_engine::Pipeline& pipeline, UdsClient& client);

    // 被主循环每 5s 调一次，不是独立线程
    void send_heartbeat();

    // 返回最近一次读取的 NPU 温度（-1 表示未读到）
    float last_npu_temp() const { return last_npu_temp_; }

private:
    gateway_engine::Pipeline& pipeline_;
    UdsClient& client_;
    float last_npu_temp_ = -1.0f;
    // 上次 tick 时刻：用于按真实间隔计算 fps（主循环 sleep(5) 非精确 1s）
    std::chrono::steady_clock::time_point last_tick_;
};
