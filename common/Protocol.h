#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__ 

#include <cstdint>
#include <vector>
#include <cstddef>

// 查询类型
enum class QueryType : uint8_t {
    // 请求
    REQ_ALARM_LIST  = 0x10,          // 告警列表
    REQ_HISTORY     = 0x11,          // 历史数据
    REQ_NODE_STATUS = 0x12,          // 节点状态

    // 响应
    RESP_ALARM_LIST = 0x20,          // 告警列表
    RESP_HISTORY    = 0x21,          // 历史数据
    RESP_NODE_STATUS= 0x22,          // 节点状态
};

// 请求
#pragma pack(push, 1)
struct QueryRequest {
    QueryType type;
    int64_t start_ts;           // 起始时间戳
    int64_t end_ts;             // 结束时间戳
    int32_t limit;              // 最多返回的条数
    int32_t node_id;            // 节点ID
};

// 响应中的每一条记录
struct ResponseRecord {
    int64_t ts;                 // 时间戳
    int32_t node_id;            // 节点ID
    union {
        struct { char type[32]; char detail[256]; } alarm;
        struct { int32_t sensor_type; float value; } sensor;
        struct { int32_t online; int64_t heartbeat; } node;
    }payload;
};

// 响应
struct QueryResponse {
    QueryType type;
    int32_t count;              // 返回的记录数
};

#pragma pack(pop)

// 编码请求
std::vector<uint8_t> encode_query_request(const QueryRequest& req);

// 解码请求
bool decode_query_request(const uint8_t* data, size_t len, QueryRequest& req);

// 构建响应
std::vector<uint8_t> build_response(const QueryResponse& resp, const std::vector<ResponseRecord>& records);

// 解码响应
bool parse_response(const uint8_t* data, size_t len, QueryResponse& resp, std::vector<ResponseRecord>& records);

#endif