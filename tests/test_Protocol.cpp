#include <gtest/gtest.h>
#include "Protocol.h"
#include <cstring>
#include "TlvProtocol.h"

// ========== 请求编解码往返 ==========

TEST(ProtocolTest, EncodeDecodeRequest) {
    QueryRequest req;
    req.type     = QueryType::REQ_ALARM_LIST;
    req.start_ts = 1719100000;
    req.end_ts   = 1719200000;
    req.limit    = 50;
    req.node_id  = 3;

    auto encoded = encode_query_request(req);

    QueryRequest decoded;
    bool ok = decode_query_request(encoded.data(), encoded.size(), decoded);
    EXPECT_TRUE(ok);
    EXPECT_EQ(decoded.type,     req.type);
    EXPECT_EQ(decoded.start_ts, req.start_ts);
    EXPECT_EQ(decoded.end_ts,   req.end_ts);
    EXPECT_EQ(decoded.limit,    req.limit);
    EXPECT_EQ(decoded.node_id,  req.node_id);
}


// ========== 请求解码拒绝非法 type ==========

TEST(ProtocolTest, DecodeRequestRejectWrongType) {
    // 手工构造一个 type=0x04（视觉告警，不是查询请求）的 TLV 报文
    Tlvpacket packet;
    packet.header.magic   = 0x5A5A;
    packet.header.version = 1;
    packet.header.type    = 0x04;   // 视觉告警，不在 0x10~0x12 范围
    packet.header.total_len = sizeof(TlvHeader) + sizeof(QueryRequest) + sizeof(uint16_t);
    packet.value.resize(sizeof(QueryRequest));
    std::memset(packet.value.data(), 0, sizeof(QueryRequest));

    auto encoded = encode_tlv(packet);

    QueryRequest req;
    EXPECT_FALSE(decode_query_request(encoded.data(), encoded.size(), req));
}

// 请求 value 大小不对
TEST(ProtocolTest, DecodeRequestRejectWrongSize) {
    Tlvpacket packet;
    packet.header.magic   = 0x5A5A;
    packet.header.version = 1;
    packet.header.type    = static_cast<uint8_t>(QueryType::REQ_HISTORY);
    packet.header.total_len = sizeof(TlvHeader) + 3 + sizeof(uint16_t); // 值只有 3 字节
    packet.value = {0x00, 0x00, 0x00};

    auto encoded = encode_tlv(packet);

    QueryRequest req;
    EXPECT_FALSE(decode_query_request(encoded.data(), encoded.size(), req));
}


// ========== 响应编解码往返 ==========

// 0 条记录
TEST(ProtocolTest, EncodeDecodeResponseEmpty) {
    QueryResponse resp;
    resp.type  = QueryType::RESP_ALARM_LIST;
    resp.count = 0;
    std::vector<ResponseRecord> records;

    auto encoded = build_response(resp, records);

    QueryResponse decoded_resp;
    std::vector<ResponseRecord> decoded_records;
    bool ok = parse_response(encoded.data(), encoded.size(), decoded_resp, decoded_records);
    EXPECT_TRUE(ok);
    EXPECT_EQ(decoded_resp.type,  resp.type);
    EXPECT_EQ(decoded_resp.count, 0);
    EXPECT_TRUE(decoded_records.empty());
}

// 多条告警记录（含变长字符串）
TEST(ProtocolTest, EncodeDecodeResponseMultiAlarm) {
    // 构造 2 条告警记录
    std::vector<ResponseRecord> records(2);
    records[0].ts      = 1719100000;
    records[0].node_id = 1;
    std::strncpy(records[0].payload.alarm.type,   "高温",   32);
    std::strncpy(records[0].payload.alarm.detail, "节点1温度85°C", 256);

    records[1].ts      = 1719103600;
    records[1].node_id = 3;
    std::strncpy(records[1].payload.alarm.type,   "视觉入侵", 32);
    std::strncpy(records[1].payload.alarm.detail, "区域B检测到移动物体", 256);

    QueryResponse resp;
    resp.type  = QueryType::RESP_ALARM_LIST;
    resp.count = static_cast<int32_t>(records.size());

    auto encoded = build_response(resp, records);

    QueryResponse decoded_resp;
    std::vector<ResponseRecord> decoded_records;
    bool ok = parse_response(encoded.data(), encoded.size(), decoded_resp, decoded_records);
    EXPECT_TRUE(ok);
    EXPECT_EQ(decoded_resp.type,  resp.type);
    ASSERT_EQ(decoded_records.size(), 2u);

    EXPECT_EQ(decoded_records[0].ts,       records[0].ts);
    EXPECT_EQ(decoded_records[0].node_id,  records[0].node_id);
    EXPECT_STREQ(decoded_records[0].payload.alarm.type,   "高温");
    EXPECT_STREQ(decoded_records[0].payload.alarm.detail, "节点1温度85°C");

    EXPECT_EQ(decoded_records[1].ts,       records[1].ts);
    EXPECT_EQ(decoded_records[1].node_id,  records[1].node_id);
    EXPECT_STREQ(decoded_records[1].payload.alarm.type,   "视觉入侵");
    EXPECT_STREQ(decoded_records[1].payload.alarm.detail, "区域B检测到移动物体");
}


// ========== 响应解析拒绝场景 ==========

// type 不是响应类型
TEST(ProtocolTest, DecodeResponseRejectWrongType) {
    Tlvpacket packet;
    packet.header.magic   = 0x5A5A;
    packet.header.version = 1;
    packet.header.type    = 0x01;   // 温湿度，不是响应
    packet.header.total_len = sizeof(TlvHeader) + sizeof(int32_t) + sizeof(uint16_t);
    int32_t zero = 0;
    packet.value.assign(reinterpret_cast<uint8_t*>(&zero),
                        reinterpret_cast<uint8_t*>(&zero) + sizeof(int32_t));

    auto encoded = encode_tlv(packet);

    QueryResponse resp;
    std::vector<ResponseRecord> records;
    EXPECT_FALSE(parse_response(encoded.data(), encoded.size(), resp, records));
}

// body 太短，连 count 都没有
TEST(ProtocolTest, DecodeResponseRejectTooShort) {
    Tlvpacket packet;
    packet.header.magic   = 0x5A5A;
    packet.header.version = 1;
    packet.header.type    = static_cast<uint8_t>(QueryType::RESP_NODE_STATUS);
    packet.header.total_len = sizeof(TlvHeader) + 2 + sizeof(uint16_t); // body 只有 2 字节
    packet.value = {0x00, 0x00};

    auto encoded = encode_tlv(packet);

    QueryResponse resp;
    std::vector<ResponseRecord> records;
    EXPECT_FALSE(parse_response(encoded.data(), encoded.size(), resp, records));
}

// count 与实际 body 大小不匹配
TEST(ProtocolTest, DecodeResponseRejectCountMismatch) {
    Tlvpacket packet;
    packet.header.magic   = 0x5A5A;
    packet.header.version = 1;
    packet.header.type    = static_cast<uint8_t>(QueryType::RESP_ALARM_LIST);

    int32_t count = 5;   // 声称 5 条记录
    // 但 body 只有 count(4B) + 1条记录
    packet.value.resize(sizeof(int32_t) + sizeof(ResponseRecord));
    memcpy(packet.value.data(), &count, sizeof(int32_t));

    packet.header.total_len = sizeof(TlvHeader) + packet.value.size() + sizeof(uint16_t);

    auto encoded = encode_tlv(packet);

    QueryResponse resp;
    std::vector<ResponseRecord> records;
    EXPECT_FALSE(parse_response(encoded.data(), encoded.size(), resp, records));
}
