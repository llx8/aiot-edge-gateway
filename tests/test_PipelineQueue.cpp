#include <gtest/gtest.h>
#include "pipeline/PipelineQueue.h"
#include <thread>
#include <chrono>

using namespace gateway_engine;

// 基本推拉
TEST(PipelineQueueTest, BasicPushPop) {
    PipelineQueue<int, 4> q(OverflowPolicy::DropOldest);
    q.push(10);
    q.push(20);
    q.push(30);

    int val;
    EXPECT_TRUE(q.pop(val));
    EXPECT_EQ(val, 10);
    EXPECT_TRUE(q.pop(val));
    EXPECT_EQ(val, 20);
    EXPECT_TRUE(q.pop(val));
    EXPECT_EQ(val, 30);
    // 队列空，pop 应失败
    EXPECT_FALSE(q.pop(val));
}

// DropOldest：推满后新数据覆盖最旧数据（SPSC 实际容量 = Capacity-1）
TEST(PipelineQueueTest, DropOldestEvictsOldest) {
    PipelineQueue<int, 3> q(OverflowPolicy::DropOldest);
    q.push(1);
    q.push(2);
    q.push(3);
    // 再推一次，应丢弃最旧的
    q.push(4);

    int val;
    EXPECT_TRUE(q.pop(val));
    EXPECT_EQ(val, 3);   // SPSC 容量 3 只能存 2，剩下 [3, 4]
    EXPECT_TRUE(q.pop(val));
    EXPECT_EQ(val, 4);
    EXPECT_FALSE(q.pop(val));
}

// DropOldest：连续满队列推送，始终保持最新 N 条（SPSC 实际容量 = Capacity-1）
TEST(PipelineQueueTest, DropOldestMultiple) {
    PipelineQueue<int, 3> q(OverflowPolicy::DropOldest);
    for (int i = 0; i < 10; ++i) {
        q.push(i);
    }
    // Capacity=3 只能存 2，所以剩下 [8, 9]
    int val;
    EXPECT_TRUE(q.pop(val));
    EXPECT_EQ(val, 8);
    EXPECT_TRUE(q.pop(val));
    EXPECT_EQ(val, 9);
    EXPECT_FALSE(q.pop(val));
}

// Block：满队列时 pop 后才允许 push（SPSC 实际容量 = Capacity-1）
TEST(PipelineQueueTest, BlockUnblocksAfterPop) {
    PipelineQueue<int, 3> q(OverflowPolicy::Block);   // 实际容量 2
    q.push(1);
    q.push(2);

    // 在另一个线程中 pop
    std::atomic<bool> popped{false};
    std::thread t([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        int val;
        EXPECT_TRUE(q.pop(val));
        EXPECT_EQ(val, 1);
        popped = true;
    });

    // push 第 3 个——队列满，会阻塞直到上面的 pop 完成
    q.push(3);
    EXPECT_TRUE(popped.load());
    t.join();

    // 验证队列内容
    int val;
    EXPECT_TRUE(q.pop(val));
    EXPECT_EQ(val, 2);
    EXPECT_TRUE(q.pop(val));
    EXPECT_EQ(val, 3);
    EXPECT_FALSE(q.pop(val));
}

// 混合类型：shared_ptr 智能指针
TEST(PipelineQueueTest, SharedPtrElements) {
    PipelineQueue<std::shared_ptr<int>, 4> q(OverflowPolicy::DropOldest);
    auto p = std::make_shared<int>(42);
    q.push(p);
    p.reset();      // 原指针释放

    std::shared_ptr<int> out;
    EXPECT_TRUE(q.pop(out));
    EXPECT_NE(out, nullptr);
    EXPECT_EQ(*out, 42);
}
