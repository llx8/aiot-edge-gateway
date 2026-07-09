#include <gtest/gtest.h>
#include "ModbusRtu.h"

// ========== 编码测试 ==========

// 基本编码：验证 TCP 帧结构 (7字节 MBAP + 5字节 PDU = 12字节)
TEST(ModbusTcpTest, EncodeBasic) {
    ModbusRequest req{1, 0x03, 0, 10};
    auto frame = encode_tcp_request(0x0001, req);

    EXPECT_EQ(frame.size(), 12u);
    // MBAP 头
    EXPECT_EQ(frame[0], 0x00);   // 事务ID 高
    EXPECT_EQ(frame[1], 0x01);   // 事务ID 低
    EXPECT_EQ(frame[2], 0x00);   // 协议ID 高
    EXPECT_EQ(frame[3], 0x00);   // 协议ID 低
    EXPECT_EQ(frame[4], 0x00);   // 长度 高
    EXPECT_EQ(frame[5], 0x06);   // 长度 = 单元ID(1) + PDU(5) = 6
    // MBAP 单元ID + PDU
    EXPECT_EQ(frame[6], 0x01);   // 单元ID (从站地址)
    EXPECT_EQ(frame[7], 0x03);   // 功能码
    EXPECT_EQ(frame[8], 0x00);   // 起始地址 高
    EXPECT_EQ(frame[9], 0x00);   // 起始地址 低
    EXPECT_EQ(frame[10], 0x00);  // 数量 高
    EXPECT_EQ(frame[11], 0x0A);  // 数量 低
}

// 无 CRC：TCP 帧末尾是 PDU 数据，不是 CRC
TEST(ModbusTcpTest, EncodeNoCrc) {
    ModbusRequest req{7, 0x03, 0x0100, 2};
    auto frame = encode_tcp_request(0x0042, req);

    EXPECT_EQ(frame.size(), 12u);
    // 最后两字节是 quantity(0x0002)，不是 CRC
    EXPECT_EQ(frame[10], 0x00);
    EXPECT_EQ(frame[11], 0x02);
}

// 事务ID 大端序
TEST(ModbusTcpTest, EncodeTransId) {
    ModbusRequest req{1, 0x03, 0, 1};
    auto frame = encode_tcp_request(0x1234, req);

    EXPECT_EQ(frame[0], 0x12);
    EXPECT_EQ(frame[1], 0x34);
}

// ========== 解码测试 ==========

// 构造合法 TCP 响应帧 (MBAP头 + PDU, 无CRC)
static std::vector<uint8_t> make_tcp_response(uint16_t trans_id, uint8_t unit_id,
                                               const std::vector<uint16_t>& regs) {
    std::vector<uint8_t> frame;
    // MBAP 头: 事务ID(2) + 协议ID(2) + 长度(2)
    frame.push_back((trans_id >> 8) & 0xFF);
    frame.push_back(trans_id & 0xFF);
    frame.push_back(0x00);  // 协议ID 高
    frame.push_back(0x00);  // 协议ID 低
    uint16_t len = 1 + 1 + regs.size() * 2;  // 单元ID + 功能码 + 数据
    frame.push_back((len >> 8) & 0xFF);
    frame.push_back(len & 0xFF);
    // 单元ID + PDU
    frame.push_back(unit_id);                 // 单元ID
    frame.push_back(0x03);                    // 功能码
    frame.push_back(static_cast<uint8_t>(regs.size() * 2));  // 字节数
    for (auto r : regs) {                     // 寄存器数据（大端）
        frame.push_back((r >> 8) & 0xFF);
        frame.push_back(r & 0xFF);
    }
    return frame;
}

// 正常解码：2 个寄存器
TEST(ModbusTcpTest, DecodeBasic) {
    auto frame = make_tcp_response(0x0001, 1, {300, 850});
    ModbusResponse resp;
    ASSERT_TRUE(decode_tcp_response(frame.data(), frame.size(), resp));
    EXPECT_EQ(resp.slave_addr, 1);
    EXPECT_EQ(resp.function_code, 0x03);
    EXPECT_EQ(resp.byte_count, 4);
    ASSERT_EQ(resp.registers.size(), 2u);
    EXPECT_EQ(resp.registers[0], 300);
    EXPECT_EQ(resp.registers[1], 850);
}

// 单寄存器
TEST(ModbusTcpTest, DecodeSingleRegister) {
    auto frame = make_tcp_response(0x0002, 5, {0xABCD});
    ModbusResponse resp;
    ASSERT_TRUE(decode_tcp_response(frame.data(), frame.size(), resp));
    EXPECT_EQ(resp.slave_addr, 5);
    EXPECT_EQ(resp.registers.size(), 1u);
    EXPECT_EQ(resp.registers[0], 0xABCD);
}

// 从站错误响应：功能码 | 0x80
TEST(ModbusTcpTest, DecodeSlaveError) {
    // MBAP头 + 单元ID=1 + 功能码=0x83 + 异常码=0x02
    std::vector<uint8_t> frame = {
        0x00, 0x01,           // 事务ID
        0x00, 0x00,           // 协议ID
        0x00, 0x03,           // 长度 = 3
        0x01,                 // 单元ID
        0x83,                 // 功能码(错误)
        0x02                  // 异常码
    };
    ModbusResponse resp;
    EXPECT_FALSE(decode_tcp_response(frame.data(), frame.size(), resp));
}

// 数据太短：最小 TCP 帧 = 9 字节 (MBAP 7 + 功能码 1 + 异常码/字节数 1)
TEST(ModbusTcpTest, DecodeTooShort) {
    uint8_t data[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x03};  // 8字节
    ModbusResponse resp;
    EXPECT_FALSE(decode_tcp_response(data, sizeof(data), resp));
}

// 刚好 9 字节（异常响应最小长度）
TEST(ModbusTcpTest, DecodeMinFrame) {
    std::vector<uint8_t> frame = {
        0x00, 0x01, 0x00, 0x00, 0x00, 0x02,  // MBAP 头
        0x01,                                   // 单元ID
        0x03,                                   // 功能码
        0x00                                    // 字节数=0
    };
    ModbusResponse resp;
    EXPECT_TRUE(decode_tcp_response(frame.data(), frame.size(), resp));
}
