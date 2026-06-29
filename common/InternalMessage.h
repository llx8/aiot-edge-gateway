#ifndef __INTERNAL_MESSAGE_H__
#define __INTERNAL_MESSAGE_H__

#include <cstdint>
#include <vector>

// 内部消息
struct InternalMessage {
    int32_t source_type; // 0 = TCP, 1 = Modbus
    int32_t node_id;      // 节点ID
    uint8_t tlv_type;    // TLV类型
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