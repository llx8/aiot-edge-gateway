#include "gtest/gtest.h"
#include "RingBuffer.h"
#include <cstring>

// ========== 基本读写 ==========

TEST(RingBufferTest, BasicReadWrite) {
    RingBuffer ring(10);
    uint8_t data[] = {1, 2, 3};
    EXPECT_TRUE(ring.append(data, 3));
    EXPECT_EQ(ring.available_to_read(), 3u);

    // read_ptr 指向写入的数据
    EXPECT_EQ(memcmp(ring.read_ptr(), data, 3), 0);

    EXPECT_TRUE(ring.consume(3));
    EXPECT_EQ(ring.available_to_read(), 0u);
}

// ========== 写满禁止超写 ==========

TEST(RingBufferTest, BufferFull) {
    RingBuffer ring(4);               // capacity=4, 最多写 3
    uint8_t data[] = {1, 2, 3, 4};
    EXPECT_TRUE(ring.append(data, 3));   // 写满 3 个
    EXPECT_FALSE(ring.append(data, 2));  // 只剩 0 个空间，拒绝
}

// ========== 消费后腾出空间 ==========

TEST(RingBufferTest, ConsumeAndWriteAgain) {
    RingBuffer ring(6);
    uint8_t d1[] = {1, 2, 3};
    uint8_t d2[] = {4, 5};

    EXPECT_TRUE(ring.append(d1, 3));
    EXPECT_TRUE(ring.consume(2));        // 吃掉 2 个，剩 1 个
    EXPECT_EQ(ring.available_to_read(), 1u);

    EXPECT_TRUE(ring.append(d2, 2));     // 再写 2 个
    EXPECT_EQ(ring.available_to_read(), 3u);
}

// ========== 跨边界环绕测试 ==========

TEST(RingBufferTest, WrapAround) {
    RingBuffer ring(6);               // capacity=6, 最多写 5, 保留 1 槽
    uint8_t d1[] = {1, 2, 3, 4};
    uint8_t d2[] = {5, 6};

    // 写 4, 消费 4 → 缓冲区空，但 write_pos=4
    EXPECT_TRUE(ring.append(d1, 4));
    EXPECT_TRUE(ring.consume(4));
    EXPECT_EQ(ring.available_to_read(), 0u);

    // 再写 2: write_pos=4 开始，位置 4,5
    EXPECT_TRUE(ring.append(d2, 2));

    // read_pos=4, write_pos=0(绕回), 可读=2
    EXPECT_EQ(ring.available_to_read(), 2u);

    // read_ptr 应指向位置 4
    EXPECT_EQ(ring.read_ptr()[0], 5);
    EXPECT_EQ(ring.read_ptr()[1], 6);

    EXPECT_TRUE(ring.consume(2));
}

// ========== 写入超过剩余空间时拒绝 ==========

TEST(RingBufferTest, WriteTooMuch) {
    RingBuffer ring(6);               // 最多写 5
    uint8_t big[] = {1, 2, 3, 4, 5, 6};
    EXPECT_FALSE(ring.append(big, 6));  // 超容量，拒绝
    EXPECT_EQ(ring.available_to_read(), 0u);  // 原子操作，什么都没留下
}

// ========== 消费超过可读数据时拒绝 ==========

TEST(RingBufferTest, ConsumeTooMuch) {
    RingBuffer ring(10);
    uint8_t d[] = {1, 2, 3};
    ring.append(d, 3);
    EXPECT_FALSE(ring.consume(5));
    EXPECT_EQ(ring.available_to_read(), 3u);   // 消费失败，数据还在
}

// ========== reset 清空 ==========

TEST(RingBufferTest, Reset) {
    RingBuffer ring(10);
    uint8_t d[] = {1, 2, 3, 4};
    ring.append(d, 4);
    ring.reset();
    EXPECT_EQ(ring.available_to_read(), 0u);
    EXPECT_EQ(ring.available_to_write(), 9u);  // capacity-1
}

// ========== 空缓冲区 read_ptr 不崩溃 ==========

TEST(RingBufferTest, ReadPtrOnEmpty) {
    RingBuffer ring(10);
    // read_ptr() 返回有效指针即可，内容不关心
    EXPECT_NE(ring.read_ptr(), nullptr);
}
