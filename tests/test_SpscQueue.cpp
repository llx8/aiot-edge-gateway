#include <gtest/gtest.h>
#include "SpscQueue.h"

// ========== 基本读写 ==========

TEST(SpscQueueTest, BasicReadWrite) {
    SpscQueue<int, 4> q;  // 实际可用 3 个
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_TRUE(q.push(3));

    int val;
    EXPECT_TRUE(q.pop(val)); EXPECT_EQ(val, 1);
    EXPECT_TRUE(q.pop(val)); EXPECT_EQ(val, 2);
    EXPECT_TRUE(q.pop(val)); EXPECT_EQ(val, 3);
}

// ========== 空队列 pop ==========

TEST(SpscQueueTest, PopEmpty) {
    SpscQueue<int, 4> q;
    int val;
    EXPECT_FALSE(q.pop(val));
    EXPECT_TRUE(q.empty());
}

// ========== 满队列 push ==========

TEST(SpscQueueTest, PushFull) {
    SpscQueue<int, 4> q;  // 实际可用 3 个
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_TRUE(q.push(3));
    EXPECT_FALSE(q.push(4));  // 第 4 个应该失败
}

// ========== 容量验证：实际可用 = Capacity - 1 ==========

TEST(SpscQueueTest, CapacityMinusOne) {
    SpscQueue<int, 4> q;
    EXPECT_TRUE(q.push(10));
    EXPECT_TRUE(q.push(20));
    EXPECT_TRUE(q.push(30));
    EXPECT_FALSE(q.push(40));  // 满了
    EXPECT_EQ(q.size(), 3u);
}

// ========== 消费后再写入 ==========

TEST(SpscQueueTest, PushAfterPop) {
    SpscQueue<int, 4> q;
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_TRUE(q.push(3));
    EXPECT_FALSE(q.push(4));  // 满了

    int val;
    EXPECT_TRUE(q.pop(val));  // 弹出 1，腾出一个位置
    EXPECT_EQ(val, 1);

    EXPECT_TRUE(q.push(4));   // 现在能写入了
    EXPECT_EQ(q.size(), 3u);
}

// ========== 循环复用：索引绕回数组开头 ==========

TEST(SpscQueueTest, WrapAround) {
    SpscQueue<int, 4> q;
    int val;

    // 第一轮：写满 + 读空，索引前进到中间位置
    EXPECT_TRUE(q.push(100));
    EXPECT_TRUE(q.push(200));
    EXPECT_TRUE(q.push(300));
    EXPECT_TRUE(q.pop(val)); EXPECT_EQ(val, 100);
    EXPECT_TRUE(q.pop(val)); EXPECT_EQ(val, 200);
    EXPECT_TRUE(q.pop(val)); EXPECT_EQ(val, 300);
    EXPECT_TRUE(q.empty());

    // 第二轮：索引已经绕回，再写读应该正常
    EXPECT_TRUE(q.push(400));
    EXPECT_TRUE(q.push(500));
    EXPECT_TRUE(q.pop(val)); EXPECT_EQ(val, 400);
    EXPECT_TRUE(q.pop(val)); EXPECT_EQ(val, 500);
    EXPECT_TRUE(q.empty());
}

// ========== size 准确性 ==========

TEST(SpscQueueTest, SizeAccuracy) {
    SpscQueue<int, 8> q;
    EXPECT_EQ(q.size(), 0u);

    q.push(1); EXPECT_EQ(q.size(), 1u);
    q.push(2); EXPECT_EQ(q.size(), 2u);
    q.push(3); EXPECT_EQ(q.size(), 3u);

    int val;
    q.pop(val); EXPECT_EQ(q.size(), 2u);
    q.pop(val); EXPECT_EQ(q.size(), 1u);
    q.pop(val); EXPECT_EQ(q.size(), 0u);
}
