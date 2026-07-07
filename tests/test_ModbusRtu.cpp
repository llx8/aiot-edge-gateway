#include <gtest/gtest.h>
#include "ModbusRtu.h"

// ========== 编码测试 ==========

// 基本编码：验证帧结构
TEST(ModbusRtuTest, EncodeBasic) {
    ModbusRequest req{1, 0x03, 0, 2};
    auto frame = encode_request(req);

    EXPECT_EQ(frame.size(), 8u);
    EXPECT_EQ(frame[0], 0x01);   // 地址
    EXPECT_EQ(frame[1], 0x03);   // 功能码
    EXPECT_EQ(frame[2], 0x00);   // 起始地址高字节
    EXPECT_EQ(frame[3], 0x00);   // 起始地址低字节
    EXPECT_EQ(frame[4], 0x00);   // 数量高字节
    EXPECT_EQ(frame[5], 0x02);   // 数量低字节

    // CRC 校验：重新计算应与帧尾一致
    uint16_t crc = crc16_modbus(frame.data(), 6);
    EXPECT_EQ(frame[6], crc & 0xFF);        // CRC 低字节在前
    EXPECT_EQ(frame[7], (crc >> 8) & 0xFF); // CRC 高字节在后
}

// 大端序验证：start_addr=0x0100
TEST(ModbusRtuTest, EncodeBigEndian) {
    ModbusRequest req{1, 0x03, 0x0100, 1};
    auto frame = encode_request(req);

    EXPECT_EQ(frame[2], 0x01);   // 高字节在前
    EXPECT_EQ(frame[3], 0x00);   // 低字节在后
}

// ========== 解码测试 ==========

// 构造合法响应帧的辅助函数
static std::vector<uint8_t> make_response(uint8_t addr, const std::vector<uint16_t>& regs) {
    std::vector<uint8_t> frame;
    frame.push_back(addr);                                       // 地址
    frame.push_back(0x03);                                       // 功能码
    frame.push_back(static_cast<uint8_t>(regs.size() * 2));     // 字节数
    for (auto r : regs) {                                        // 寄存器数据（大端）
        frame.push_back((r >> 8) & 0xFF);
        frame.push_back(r & 0xFF);
    }
    uint16_t crc = crc16_modbus(frame.data(), frame.size());
    frame.push_back(crc & 0xFF);                                 // CRC 低字节
    frame.push_back((crc >> 8) & 0xFF);                          // CRC 高字节
    return frame;
}

// 正常解码：2个寄存器
TEST(ModbusRtuTest, DecodeBasic) {
    auto frame = make_response(1, {300, 850});
    ModbusResponse resp;
    ASSERT_TRUE(decode_response(frame.data(), frame.size(), resp));
    EXPECT_EQ(resp.slave_addr, 1);
    EXPECT_EQ(resp.function_code, 0x03);
    EXPECT_EQ(resp.byte_count, 4);
    ASSERT_EQ(resp.registers.size(), 2u);
    EXPECT_EQ(resp.registers[0], 300);
    EXPECT_EQ(resp.registers[1], 850);
}

// CRC 错误：篡改一个字节
TEST(ModbusRtuTest, DecodeCrcError) {
    auto frame = make_response(1, {100});
    frame[3] ^= 0xFF;  // 篡改数据
    ModbusResponse resp;
    EXPECT_FALSE(decode_response(frame.data(), frame.size(), resp));
}

// 从站错误响应：功能码 0x83
TEST(ModbusRtuTest, DecodeSlaveError) {
    // 手造错误帧：地址=1, 功能码=0x83, 异常码=0x02
    std::vector<uint8_t> frame = {0x01, 0x83, 0x02};
    uint16_t crc = crc16_modbus(frame.data(), frame.size());
    frame.push_back(crc & 0xFF);
    frame.push_back((crc >> 8) & 0xFF);
    ModbusResponse resp;
    EXPECT_FALSE(decode_response(frame.data(), frame.size(), resp));
}

// 数据太短
TEST(ModbusRtuTest, DecodeTooShort) {
    uint8_t data[] = {0x01, 0x03, 0x04};  // 只有3字节
    ModbusResponse resp;
    EXPECT_FALSE(decode_response(data, sizeof(data), resp));
}
