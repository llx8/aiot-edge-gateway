#include "Protocol.h"
#include <cstring>

// 编码请求
std::vector<uint8_t> encode_query_request(const QueryRequest& req){
    Tlvpacket packet;
    packet.header.magic = 0x5A5A;
    packet.header.version = 1;
    packet.header.type = static_cast<uint8_t>(req.type);

    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&req);
    packet.value.assign(raw, raw + sizeof(QueryRequest));

    packet.header.total_len = sizeof(TlvHeader) + packet.value.size() + sizeof(uint16_t);

    return encode_tlv(packet);
}

// 解码请求
bool decode_query_request(const uint8_t* data, size_t len, QueryRequest& req){
    Tlvpacket packet;
    if(!decode_tlv(data, len, packet)){
        return false;
    }

    // 请求类型必须是查询
    uint8_t t = packet.header.type;
    if(t != static_cast<uint8_t>(QueryType::REQ_ALARM_LIST) &&
       t != static_cast<uint8_t>(QueryType::REQ_HISTORY) &&
       t != static_cast<uint8_t>(QueryType::REQ_NODE_STATUS)){
        return false;
    }

    // value大小必须是等于QueryRequest大小
    if (packet.value.size() != sizeof(QueryRequest)){
        return false;
    }

    memcpy(&req, packet.value.data(), packet.value.size());
    return true;
}

// 构建响应
std::vector<uint8_t> build_response(const QueryResponse& resp, const std::vector<ResponseRecord>& records){
    size_t body_size = sizeof(int32_t) + records.size() * sizeof(ResponseRecord);
    std::vector<uint8_t> body(body_size);

    // body的前面先写有几个records
    int32_t count  = static_cast<int32_t>(records.size());
    memcpy(body.data(), &count, sizeof(int32_t));
    
    // 后面依次加入
    for(size_t i = 0; i < records.size(); i++){
        size_t offset = sizeof(int32_t) + i * sizeof(ResponseRecord);
        memcpy(body.data() + offset, &records[i], sizeof(ResponseRecord));
    }

    Tlvpacket packet;
    packet.header.magic = 0x5A5A;
    packet.header.version = 1;
    packet.header.type = static_cast<uint8_t>(resp.type);
    packet.value = std::move(body);
    packet.header.total_len = sizeof(TlvHeader) + packet.value.size() + sizeof(uint16_t);

    return encode_tlv(packet);
}

// 解析响应
bool parse_response(const uint8_t* data, size_t len, QueryResponse& resp, std::vector<ResponseRecord>& records){ 
    Tlvpacket packet;
    if (!decode_tlv(data, len, packet)){
        return false;
    }

    // 响应类型必须是查询
    uint8_t t = packet.header.type;
    if (t != static_cast<uint8_t>(QueryType::RESP_ALARM_LIST) &&
       t != static_cast<uint8_t>(QueryType::RESP_HISTORY) &&
       t != static_cast<uint8_t>(QueryType::RESP_NODE_STATUS)){
        return false;
    }

    
    const uint8_t* body = packet.value.data();
    size_t body_size = packet.value.size();
    // 如果还没有前面长度的字节数大直接返回错误
    if (body_size < sizeof(int32_t)){
        return false;
    }

    int32_t count = 0;
    memcpy(&count, body, sizeof(int32_t));
    body += sizeof(int32_t);
    body_size -= sizeof(int32_t);

    // 如果后面的和前面个数描述的不一样也直接返回错误
    if(body_size != static_cast<size_t>(count) * sizeof(ResponseRecord)){
        return false;
    }

    resp.type  = static_cast<QueryType>(packet.header.type);
    resp.count = count;   
    
    // 将数据放入record中
    records.clear();
    records.reserve(count);
    for(int32_t i = 0; i < count; i++){
        ResponseRecord record;
        memcpy(&record, body + i * sizeof(ResponseRecord), sizeof(ResponseRecord));
        records.push_back(record);
    }
    return true;
}