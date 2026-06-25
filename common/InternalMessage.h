#ifndef __INTERNAL_MESSAGE_H__
#define __INTERNAL_MESSAGE_H__

#include <cstdint>
#include <vector>

// 标识来源类型
enum class SourceType : uint8_t {
    TCP_SENSOR = 0x01,
    // MODBUS_RTU = 0x02 // M2阶段打开
};

// 协议头
#pragma pack(push, 1)
struct InternalMsgHeader {
    SourceType src_type;      // 来源类型
    int64_t timestamp_ms;  // 时间戳
    uint32_t payload_len;  // 数据长度
};
#pragma pack(pop)

// 内部消息
struct InternalMessage {
    InternalMsgHeader header;    // 头
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