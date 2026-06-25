#include <gtest/gtest.h>
#include "TlvProtocol.h"

// ========== CRC16 基础测试 ==========

// 已知向量验证
TEST(TlvProtocolTest, Crc16Modbus) {
    uint8_t data[] = {0x5A, 0x5A, 0x01, 0x00, 0x0A, 0x01};
    uint16_t crc = crc16_modbus(data, sizeof(data));
    EXPECT_EQ(crc, 0x7092);
}

// 空数据 CRC16 应为 0xFFFF
TEST(TlvProtocolTest, Crc16Empty) {
    uint16_t crc = crc16_modbus(nullptr, 0);
    EXPECT_EQ(crc, 0xFFFF);
}

// CRC16 不同数据不应相同
TEST(TlvProtocolTest, Crc16DifferentData) {
    uint8_t a[] = {0x01, 0x02, 0x03};
    uint8_t b[] = {0x01, 0x02, 0x04};
    EXPECT_NE(crc16_modbus(a, 3), crc16_modbus(b, 3));
}


// ========== 编解码往返测试 ==========

TEST(TlvProtocolTest, EncodeDecodeRoundtrip) {
    Tlvpacket packet;
    packet.header.magic   = 0x5A5A;
    packet.header.version = 1;
    packet.header.total_len = 10;       // 会被 encode_tlv 按实际计算，这里随意
    packet.header.type    = 0x01;
    packet.value = {0x01, 0x02};

    auto encoded = encode_tlv(packet);

    Tlvpacket decoded;
    bool ok = decode_tlv(encoded.data(), encoded.size(), decoded);
    EXPECT_TRUE(ok);
    EXPECT_EQ(decoded.header.magic,   packet.header.magic);
    EXPECT_EQ(decoded.header.version, packet.header.version);
    EXPECT_EQ(decoded.header.type,    packet.header.type);
    EXPECT_EQ(decoded.value,          packet.value);
}

// 空 value 也能正常编解码
TEST(TlvProtocolTest, EncodeDecodeEmptyValue) {
    Tlvpacket packet;
    packet.header.magic   = 0x5A5A;
    packet.header.version = 1;
    packet.header.total_len = sizeof(TlvHeader) + sizeof(uint16_t);  // 0 字节 value
    packet.header.type    = 0xFF;
    // value 留空

    auto encoded = encode_tlv(packet);

    Tlvpacket decoded;
    bool ok = decode_tlv(encoded.data(), encoded.size(), decoded);
    EXPECT_TRUE(ok);
    EXPECT_EQ(decoded.value.size(), 0u);
}


// ========== 解码失败场景 ==========

// 数据太短（连 header 都不够）
TEST(TlvProtocolTest, DecodeFailTooShort) {
    uint8_t data[5] = {0};  // 至少需要 sizeof(TlvHeader) + 2 = 8 字节
    Tlvpacket packet;
    EXPECT_FALSE(decode_tlv(data, sizeof(data), packet));
}

// total_len 与实际长度不匹配
TEST(TlvProtocolTest, DecodeFailLengthMismatch) {
    Tlvpacket packet;
    packet.header.magic   = 0x5A5A;
    packet.header.version = 1;
    packet.header.total_len = 100;   // 假的，跟实际不一样
    packet.header.type    = 0x01;
    packet.value = {0x01};

    auto encoded = encode_tlv(packet);

    // 传一个错误的长度进去
    Tlvpacket decoded;
    EXPECT_FALSE(decode_tlv(encoded.data(), encoded.size() - 3, decoded));
}

// CRC 校验失败（篡改数据）
TEST(TlvProtocolTest, DecodeFailCrcMismatch) {
    Tlvpacket packet;
    packet.header.magic   = 0x5A5A;
    packet.header.version = 1;
    packet.header.total_len = 10;
    packet.header.type    = 0x01;
    packet.value = {0x01, 0x02};

    auto encoded = encode_tlv(packet);

    // 篡改 value 中的一个字节
    encoded[7] ^= 0xFF;

    Tlvpacket decoded;
    EXPECT_FALSE(decode_tlv(encoded.data(), encoded.size(), decoded));
}
