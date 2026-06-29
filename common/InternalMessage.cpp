#include "InternalMessage.h"
#include <cstdint>
#include <cstring>
#include <arpa/inet.h>
#include <endian.h>
#include <vector>

static constexpr size_t header_size = sizeof(int32_t) + sizeof(int32_t) + sizeof(uint8_t) + sizeof(uint32_t); // source_type + node_id + tlv_type + payload_len

// 编码
std::vector<uint8_t> encode_internal_msg(const InternalMessage &msg){
    
    // 分配空间
    std::vector<uint8_t> buf(header_size + msg.payload.size());

    // 写入头
    uint32_t source_type_net = htonl(static_cast<uint32_t>(msg.source_type));
    memcpy(buf.data(), &source_type_net, sizeof(uint32_t));

    uint32_t node_id_net = htonl(static_cast<uint32_t>(msg.node_id));
    memcpy(buf.data() + sizeof(uint32_t), &node_id_net, sizeof(uint32_t));

    uint8_t tlv_type_net = msg.tlv_type;
    memcpy(buf.data() + sizeof(uint32_t) + sizeof(uint32_t), &tlv_type_net, sizeof(uint8_t));

    // 写入payload长度
    uint32_t payload_len_net = htonl(static_cast<uint32_t>(msg.payload.size()));
    memcpy(buf.data() + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t), &payload_len_net, sizeof(uint32_t)); 

    // 写入payload
    if (!msg.payload.empty()){
        memcpy(buf.data() + header_size, msg.payload.data(), msg.payload.size());
    }

    return buf;
}

// 解码
DecodeResult decode_internal_msg(const uint8_t* data, size_t len){
    DecodeResult result{false, 0, {}};

    // 1. 检查长度
    if (len < header_size){
        return result;
    }

    // 2. 读取头
    uint32_t source_type_net = 0;
    memcpy(&source_type_net, data, sizeof(uint32_t));
    result.msg.source_type = static_cast<int32_t>(ntohl(source_type_net));

    // 3. 读取node_id
    // 主机字节序转换
    uint32_t node_id_net = 0;
    memcpy(&node_id_net, data + sizeof(uint32_t), sizeof(uint32_t));
    result.msg.node_id = static_cast<int32_t>(ntohl(node_id_net));

    // 4. 读取tlv_type
    uint8_t tlv_type_net = 0;
    memcpy(&tlv_type_net, data + sizeof(uint32_t) + sizeof(uint32_t), sizeof(uint8_t));
    result.msg.tlv_type = tlv_type_net;

    // 读取payload长度
    uint32_t payload_len_net = 0;
    memcpy(&payload_len_net, data + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t), sizeof(uint32_t));
    uint32_t payload_len = ntohl(payload_len_net);  

    // 5. 检查长度是否匹配
    if (len < header_size + payload_len){
        return result;
    }
    
    // 6. 读取payload
    if (payload_len > 0){
        result.msg.payload.assign(data + header_size, data + header_size + payload_len);
    }

    result.ok = true;
    result.consumed = header_size + payload_len;

    return result;
}