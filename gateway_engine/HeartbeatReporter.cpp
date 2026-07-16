#include "HeartbeatReporter.h"
#include "InternalMessage.h"
#include <fstream>
#include <string>

HeartbeatReporter::HeartbeatReporter(gateway_engine::Pipeline& pipeline, UdsClient& client)
    : pipeline_(pipeline)
    , client_(client)
    , last_tick_(std::chrono::steady_clock::now()) {}

static float read_npu_temp() {
    static const char* kPaths[] = {
        "/sys/class/thermal/thermal_zone6/temp",   // npu-thermal (RK3588)
        "/sys/class/npu/npu_temp",
        "/sys/devices/platform/fead0000.npu/thermal/npu_temp",
        "/sys/class/thermal/thermal_zone0/temp",
        "/sys/class/thermal/thermal_zone1/temp",
    };

    for (const char* path : kPaths) {
        std::ifstream f(path);
        if (!f.is_open()) continue;
        std::string val;
        std::getline(f, val);
        if (val.empty()) continue;
        try {
            float temp = std::stof(val);
            if (temp > 1000.0f) temp /= 1000.0f;
            return temp;
        } catch (...) {
            continue;
        }
    }
    return -1.0f;
}

void HeartbeatReporter::send_heartbeat() {
    // 按真实时间间隔计算 fps：主循环 sleep(5) 调一次本函数，
    // Pipeline::tick_fps() 返回的是"自上次 tick 以来的帧数"（非每秒帧数），
    // 若直接当作 fps 上报，25fps 流会报成 125（5s 累计帧数）。
    auto now = std::chrono::steady_clock::now();
    float elapsed_sec = std::chrono::duration<float>(now - last_tick_).count();
    last_tick_ = now;
    if (elapsed_sec <= 0.0f) elapsed_sec = 1.0f;  // 防除零（首次/时钟回退）

    pipeline_.tick_fps();  // 采样并重置帧计数
    float fps = pipeline_.fps() / elapsed_sec;
    last_npu_temp_ = read_npu_temp();
    float npu_temp = last_npu_temp_;

    InternalMessage msg;
    msg.source_type = 3;
    msg.tlv_type = 0x05;
    msg.payload.resize(8);
    memcpy(msg.payload.data(), &fps, 4);
    memcpy(msg.payload.data() + 4, &npu_temp, 4);

    auto encoded = encode_internal_msg(msg);
    client_.write(encoded.data(), encoded.size());
}
