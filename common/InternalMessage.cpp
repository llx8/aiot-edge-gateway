#include "InternalMessage.h"
#include <cstdint>
#include <cstring>
#include <arpa/inet.h>
#include <endian.h>
#include <vector>

static constexpr size_t kHeaderSize = sizeof(InternalMsgHeader);

// 编码
std::vector<uint8_t> encode_internal_msg(const InternalMessage &msg){
    // 分配空间
    std::vector<uint8_t> buf(kHeaderSize + msg.payload.size());

    // 写入头
    buf[0] = static_cast<uint8_t>(msg.header.src_type);
    // 主机字节序转换
    uint64_t timestamp_ms_net = htobe64(msg.header.timestamp_ms);

    memcpy(buf.data() + sizeof(uint8_t), &timestamp_ms_net, sizeof(uint64_t));

    uint32_t payload_len_net = htobe32(msg.payload.size());

    memcpy(buf.data() + sizeof(uint8_t) + sizeof(uint64_t), &payload_len_net, sizeof(uint32_t));

    // 写入payload
    if (!msg.payload.empty()){
        memcpy(buf.data() + kHeaderSize, msg.payload.data(), msg.payload.size());
    }

    return buf;
}

// 解码
DecodeResult decode_internal_msg(const uint8_t* data, size_t len){
    DecodeResult result{false, 0, {}};

    // 1. 检查长度
    if (len < kHeaderSize){
        return result;
    }

    // 2. 读取头
    result.msg.header.src_type = static_cast<SourceType>(data[0]);

    // 3. 读取时间戳
    // 主机字节序转换
    uint64_t timestamp_ms_net = 0;
    memcpy(&timestamp_ms_net, data + sizeof(uint8_t), sizeof(uint64_t));
    result.msg.header.timestamp_ms = be64toh(timestamp_ms_net);

    // 4. 读取payload长度
    uint32_t payload_len_net = 0;
    memcpy(&payload_len_net, data + sizeof(uint8_t) + sizeof(uint64_t), sizeof(uint32_t));
    result.msg.header.payload_len = be32toh(payload_len_net);

    // 5. 检查长度是否匹配
    if (len < kHeaderSize + result.msg.header.payload_len){
        return result;
    }
    
    // 6. 读取payload
    if (result.msg.header.payload_len > 0){
        result.msg.payload.assign(data + kHeaderSize, data + kHeaderSize + result.msg.header.payload_len);
    }

    result.ok = true;
    result.consumed = kHeaderSize + result.msg.header.payload_len;

    return result;
}