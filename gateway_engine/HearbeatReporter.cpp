#include "HearbeatReporter.h"
#include "InternalMessage.h" 

// 构造函数
HeartbeatReporter::HeartbeatReporter(gateway_engine::Pipeline& pipeline, UdsClient& client) 
    : pipeline_(pipeline)
    , client_(client) {}

void HeartbeatReporter::send_heartbeat() {
    float fps = pipeline_.fps();         // 拿帧率
    float npu_temp = 45.0f;              // PC 上 mock 固定值

    InternalMessage msg;
    msg.source_type = 3;                 // AI Detection
    msg.tlv_type = 0x05;                 // AI 心跳
    msg.payload.resize(8);               // 4B fps + 4B npu_temp
    memcpy(msg.payload.data(), &fps, 4);
    memcpy(msg.payload.data() + 4, &npu_temp, 4);

    auto encoded = encode_internal_msg(msg);
    client_.write(encoded.data(), encoded.size());
}
