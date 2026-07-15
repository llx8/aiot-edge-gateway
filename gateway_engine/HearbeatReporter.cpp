#include "HearbeatReporter.h"
#include "InternalMessage.h" 
#include <fstream>
#include <string>

HeartbeatReporter::HeartbeatReporter(gateway_engine::Pipeline& pipeline, UdsClient& client) 
    : pipeline_(pipeline)
    , client_(client) {}

static float read_npu_temp() {
    static const char* kPaths[] = {
        "/sys/class/npu/npu_temp",
        "/sys/devices/platform/fead0000.npu/thermal/npu_temp",
        "/sys/class/thermal/thermal_zone0/temp",
        "/sys/class/thermal/thermal_zone1/temp",
        "/sys/class/thermal/thermal_zone2/temp",
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
    float fps = pipeline_.fps();
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
