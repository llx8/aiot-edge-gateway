#include <gtest/gtest.h>
#include "InternalMessage.h"

// ========== 编解码往返测试 ==========

TEST(InternalMessageTest, RoundTrip) {
    InternalMessage msg;
    msg.header.src_type     = SourceType::TCP_SENSOR;
    msg.header.timestamp_ms = 1718895600000;
    msg.header.payload_len  = 4;          // encode 会按实际重算，这里随意
    msg.payload = {0xAA, 0xBB, 0xCC, 0xDD};

    auto encoded = encode_internal_msg(msg);
    ASSERT_GT(encoded.size(), 0u);

    auto result = decode_internal_msg(encoded.data(), encoded.size());
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.msg.header.src_type,     SourceType::TCP_SENSOR);
    EXPECT_EQ(result.msg.header.timestamp_ms, 1718895600000);
    EXPECT_EQ(result.msg.payload,             msg.payload);
    EXPECT_EQ(result.consumed,                encoded.size());
}

TEST(InternalMessageTest, EmptyPayload) {
    InternalMessage msg;
    msg.header.src_type     = SourceType::TCP_SENSOR;
    msg.header.timestamp_ms = 0;
    msg.header.payload_len  = 0;

    auto encoded = encode_internal_msg(msg);
    ASSERT_GT(encoded.size(), 0u);

    auto result = decode_internal_msg(encoded.data(), encoded.size());
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.msg.payload.size(), 0u);
}

// ========== 解码失败场景 ==========

TEST(InternalMessageTest, DataTooShortHeader) {
    uint8_t data[5] = {0};   // 不够 13 字节 header
    auto result = decode_internal_msg(data, sizeof(data));
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.consumed, 0u);
}

TEST(InternalMessageTest, DataTooShortBody) {
    InternalMessage msg;
    msg.header.src_type     = SourceType::TCP_SENSOR;
    msg.header.timestamp_ms = 1000;
    msg.payload             = {0x01, 0x02, 0x03, 0x04};

    auto encoded = encode_internal_msg(msg);

    // 截断：只要 header + 部分 body
    size_t trunc_len = encoded.size() - 2;
    auto result = decode_internal_msg(encoded.data(), trunc_len);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.consumed, 0u);
}

// ========== UDS 粘包场景 ==========

TEST(InternalMessageTest, StickyPackets) {
    // 构造两条消息
    InternalMessage msg1;
    msg1.header.src_type     = SourceType::TCP_SENSOR;
    msg1.header.timestamp_ms = 1000;
    msg1.payload             = {0x11, 0x22};

    InternalMessage msg2;
    msg2.header.src_type     = SourceType::TCP_SENSOR;
    msg2.header.timestamp_ms = 2000;
    msg2.payload             = {0x33, 0x44, 0x55};

    auto enc1 = encode_internal_msg(msg1);
    auto enc2 = encode_internal_msg(msg2);

    // 粘在一起（模拟 UDS 粘包）
    std::vector<uint8_t> sticky;
    sticky.insert(sticky.end(), enc1.begin(), enc1.end());
    sticky.insert(sticky.end(), enc2.begin(), enc2.end());

    // 第一轮解码：拿到 msg1
    auto r1 = decode_internal_msg(sticky.data(), sticky.size());
    ASSERT_TRUE(r1.ok);
    EXPECT_EQ(r1.msg.header.timestamp_ms, 1000);
    EXPECT_EQ(r1.msg.payload, msg1.payload);
    EXPECT_EQ(r1.consumed, enc1.size());

    // 第二乱解码：从剩余数据拿到 msg2
    size_t remaining = sticky.size() - r1.consumed;
    ASSERT_GT(remaining, 0u);
    auto r2 = decode_internal_msg(sticky.data() + r1.consumed, remaining);
    ASSERT_TRUE(r2.ok);
    EXPECT_EQ(r2.msg.header.timestamp_ms, 2000);
    EXPECT_EQ(r2.msg.payload, msg2.payload);
    EXPECT_EQ(r2.consumed, enc2.size());
}
