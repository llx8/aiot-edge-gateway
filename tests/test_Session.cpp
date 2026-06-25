#include <gtest/gtest.h>
#include "Session.h"
#include "TlvProtocol.h"

// 辅助函数：构造一个合法的完整 TLV 报文（二进制）
std::vector<uint8_t> make_packet(uint8_t type, const std::vector<uint8_t>& value) {
    Tlvpacket pkt;
    pkt.header.magic = 0x5A5A;
    pkt.header.version = 1;
    pkt.header.type = type;
    pkt.value = value;
    pkt.header.total_len = sizeof(TlvHeader) + value.size() + sizeof(uint16_t);
    return encode_tlv(pkt);
}

// ==================== 空数据处理 ====================
TEST(SessionTest, EmptyData_ReturnsNothing) {
    Session session(0);   // fd=0 用于测试
    auto result = session.handle_data(nullptr, 0);
    EXPECT_TRUE(result.empty());
}

// ==================== 单个完整包 ====================
TEST(SessionTest, SinglePacket_ReturnsOne) {
    Session session(0);
    auto raw = make_packet(0x01, {0x10, 0x20, 0x30});

    auto result = session.handle_data(raw.data(), raw.size());
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], raw);
}

// ==================== 两个粘包一次收 ====================
TEST(SessionTest, TwoPacketsSticky_ReturnsTwo) {
    Session session(0);
    auto pkt1 = make_packet(0x01, {0x01, 0x02});
    auto pkt2 = make_packet(0xFF, {0x03, 0x04});

    // 拼在一起模拟粘包
    std::vector<uint8_t> combined;
    combined.insert(combined.end(), pkt1.begin(), pkt1.end());
    combined.insert(combined.end(), pkt2.begin(), pkt2.end());

    auto result = session.handle_data(combined.data(), combined.size());
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], pkt1);
    EXPECT_EQ(result[1], pkt2);
}

// ==================== 半包：分两次收 ====================
TEST(SessionTest, FragmentedPacket_Reassembles) {
    Session session(0);
    auto raw = make_packet(0x01, {0xAA, 0xBB, 0xCC, 0xDD});

    // 第一次只发 4 字节（不够 header）
    auto result1 = session.handle_data(raw.data(), 4);
    EXPECT_TRUE(result1.empty());

    // 第二次发剩余的
    auto result2 = session.handle_data(raw.data() + 4, raw.size() - 4);
    ASSERT_EQ(result2.size(), 1);
    EXPECT_EQ(result2[0], raw);
}

// ==================== 多个半包逐步到达 ====================
TEST(SessionTest, MultipleFragments_ReturnsPackets) {
    Session session(0);
    auto pkt1 = make_packet(0x01, {0x11, 0x22});
    auto pkt2 = make_packet(0x02, {0x33});

    std::vector<uint8_t> combined;
    combined.insert(combined.end(), pkt1.begin(), pkt1.end());
    combined.insert(combined.end(), pkt2.begin(), pkt2.end());

    // 分三次喂入
    auto r1 = session.handle_data(combined.data(), 3);          // 3 字节
    EXPECT_TRUE(r1.empty());

    auto r2 = session.handle_data(combined.data() + 3, 5);       // 再 5 字节
    // 此时应该解出 pkt1

    auto r3 = session.handle_data(combined.data() + 8, combined.size() - 8);  // 剩余的
    // 此时应该解出 pkt2

    // pkt1 和 pkt2 分别在 r2 或 r3 中（取决于具体字节数和位置）
    int total = (int)(r1.size() + r2.size() + r3.size());
    EXPECT_EQ(total, 2);
}

// ==================== 前面有垃圾字节 ====================
TEST(SessionTest, GarbageBeforeMagic_Skipped) {
    Session session(0);
    auto raw = make_packet(0xFF, {0x42});

    std::vector<uint8_t> with_garbage;
    with_garbage.push_back(0x00);   // 垃圾
    with_garbage.push_back(0xFF);   // 垃圾
    with_garbage.push_back(0xAB);   // 垃圾
    with_garbage.insert(with_garbage.end(), raw.begin(), raw.end());

    auto result = session.handle_data(with_garbage.data(), with_garbage.size());
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], raw);
}

// ==================== 假 magic（第一个 0x5A 后面不是 0x5A） ====================
TEST(SessionTest, FakeMagic_Skipped) {
    Session session(0);
    auto raw = make_packet(0x01, {0x77});

    std::vector<uint8_t> data;
    data.push_back(0x5A);  // 假 magic（后面不是 0x5A）
    data.push_back(0x01);  // 不是 0x5A
    data.insert(data.end(), raw.begin(), raw.end());

    auto result = session.handle_data(data.data(), data.size());
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], raw);
}

// ==================== total_len 不合法（过大） ====================
TEST(SessionTest, InvalidTotalLenTooLarge_Discarded) {
    Session session(0);

    // 手动构造一个 total_len=5000 的 header（数据没那么长）
    std::vector<uint8_t> bad;
    bad.push_back(0x5A);   // magic H
    bad.push_back(0x5A);   // magic L
    bad.push_back(0x01);   // version
    bad.push_back(0x13);   // total_len H  → 0x1388 = 5000
    bad.push_back(0x88);   // total_len L
    bad.push_back(0x01);   // type

    auto result = session.handle_data(bad.data(), bad.size());
    // total_len 5000 > kMaxPacketSize(4096)，应被丢弃，回到 IDLE
    EXPECT_TRUE(result.empty());
}

// ==================== total_len 不合法（过小） ====================
TEST(SessionTest, InvalidTotalLenTooSmall_Discarded) {
    Session session(0);

    std::vector<uint8_t> bad;
    bad.push_back(0x5A);   // magic H
    bad.push_back(0x5A);   // magic L
    bad.push_back(0x01);   // version
    bad.push_back(0x00);   // total_len H
    bad.push_back(0x05);   // total_len L → 5（小于 header+crc=8）
    bad.push_back(0x01);   // type

    auto result = session.handle_data(bad.data(), bad.size());
    EXPECT_TRUE(result.empty());
}

// ==================== CRC 校验失败 ====================
TEST(SessionTest, CrcMismatch_Discarded) {
    Session session(0);
    auto raw = make_packet(0x01, {0xAA, 0xBB});

    // 篡改 value 区的某个字节使 CRC 不匹配
    std::vector<uint8_t> corrupted = raw;
    corrupted[sizeof(TlvHeader)] ^= 0xFF;   // 翻转 value 第一个字节

    auto result = session.handle_data(corrupted.data(), corrupted.size());
    // CRC 不通过，应丢弃
    EXPECT_TRUE(result.empty());
}

// ==================== 连续多个 CRC 失败后回来一个正确的 ====================
TEST(SessionTest, CrcFailThenSuccess) {
    Session session(0);
    auto good = make_packet(0x01, {0x11, 0x22});

    // 先发一个坏包
    auto bad = good;
    bad[sizeof(TlvHeader)] ^= 0xFF;
    std::vector<uint8_t> combined;
    combined.insert(combined.end(), bad.begin(), bad.end());
    combined.insert(combined.end(), good.begin(), good.end());

    auto result = session.handle_data(combined.data(), combined.size());
    // 坏包丢弃，好包返回
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], good);
}

// ==================== 连续多次 handle_data ====================
TEST(SessionTest, MultipleHandleDataCalls) {
    Session session(0);

    for (int i = 0; i < 10; i++) {
        auto raw = make_packet(0x01, {static_cast<uint8_t>(i)});
        auto result = session.handle_data(raw.data(), raw.size());
        ASSERT_EQ(result.size(), 1);
        EXPECT_EQ(result[0], raw);
    }
}
