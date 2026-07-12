#ifndef __INTERNAL_MESSAGE_H__
#define __INTERNAL_MESSAGE_H__

#include <cstdint>
#include <vector>

constexpr uint8_t CMD_START_ANALYSIS = 0x10;
constexpr uint8_t CMD_STOP_ANALYSIS  = 0x11;
constexpr uint8_t CMD_SWITCH_MODEL    = 0x12;
constexpr uint8_t CMD_MODEL_ACK       = 0x13;
constexpr uint8_t CMD_MODEL_NACK      = 0x14;

// 内部消息
struct InternalMessage {
    int32_t source_type; // 0 = Modbus, 1 = GPIO, 2 = I2C, 3 = AI_DETECTION
    int32_t node_id;      // 节点ID
    uint8_t tlv_type;    // 0x01=传感器, 0x04 = AI视觉告警, 0x05 = AI心跳, 0xFF = 设备心跳
    std::vector<uint8_t> payload; // 数据
};

// 解码结果
struct DecodeResult {
    bool ok;                 // 是否成功
    size_t consumed;         // 已消费的字节数
    InternalMessage msg;     // 消息
};

// 接口
// 编码
std::vector<uint8_t> encode_internal_msg(const InternalMessage& msg);

// 解码
DecodeResult decode_internal_msg(const uint8_t* data, size_t length);

#endif