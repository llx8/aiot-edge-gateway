#include <gtest/gtest.h>
#include "pipeline/FramePool.h"
#include <thread>
#include <vector>

using namespace gateway_engine;

// 基本分配：拿到帧后 data 指针不为空
TEST(FramePoolTest, AllocReturnsValidFrame) {
    FramePool pool(4, 640, 480, 3);
    auto frame = pool.get_frame();
    ASSERT_NE(frame, nullptr);
    EXPECT_NE(frame->data, nullptr);
    EXPECT_NE(frame->preprocessed_data, nullptr);
    EXPECT_EQ(frame->width, 640);
    EXPECT_EQ(frame->height, 480);
    EXPECT_EQ(frame->channels, 3);
}

// 耗尽：分配完 pool_size 帧后，下一帧返回 nullptr
TEST(FramePoolTest, ExhaustionReturnsNull) {
    FramePool pool(3, 320, 240, 3);
    std::vector<std::shared_ptr<Frame>> frames;
    for (int i = 0; i < 3; ++i) {
        auto f = pool.get_frame();
        ASSERT_NE(f, nullptr) << "第 " << i << " 次分配本应成功";
        frames.push_back(std::move(f));
    }
    // 第 4 次应失败
    auto f = pool.get_frame();
    EXPECT_EQ(f, nullptr);
}

// 归还后可以重新分配
TEST(FramePoolTest, FreeAndRealloc) {
    FramePool pool(2, 320, 240, 3);
    {
        auto f1 = pool.get_frame();
        ASSERT_NE(f1, nullptr);
        auto f2 = pool.get_frame();
        ASSERT_NE(f2, nullptr);
        // f1, f2 在此释放
    }
    // 归还后应能再次分配
    auto f3 = pool.get_frame();
    ASSERT_NE(f3, nullptr);
    auto f4 = pool.get_frame();
    ASSERT_NE(f4, nullptr);
}

// 分配完所有帧 → 归还一个 → 再分配成功
TEST(FramePoolTest, FreeOneThenRealloc) {
    FramePool pool(3, 320, 240, 3);
    std::vector<std::shared_ptr<Frame>> frames;
    for (int i = 0; i < 3; ++i) {
        frames.push_back(pool.get_frame());
    }
    // 释放第 2 帧
    frames[1].reset();
    // 此时应可再分配一帧
    auto f = pool.get_frame();
    EXPECT_NE(f, nullptr);
}

// 帧数据可写入（验证 data 指针指向可写内存）
TEST(FramePoolTest, FrameDataIsWritable) {
    FramePool pool(2, 64, 64, 3);
    auto frame = pool.get_frame();
    ASSERT_NE(frame, nullptr);
    // 写几字节进去，不 segfault 即通过
    frame->data[0] = 0xAB;
    frame->data[100] = 0xCD;
    frame->data[frame->width * frame->height * frame->channels - 1] = 0xEF;
    EXPECT_EQ(frame->data[0], 0xAB);
}

// 并发分配/归还，跑 100 轮不崩
TEST(FramePoolTest, ConcurrentAllocFree) {
    constexpr int kPoolSize = 8;
    constexpr int kNumThreads = 4;
    constexpr int kIterations = 50;
    FramePool pool(kPoolSize, 320, 240, 3);

    std::vector<std::thread> threads;
    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back([&pool]() {
            for (int i = 0; i < kIterations; ++i) {
                auto f = pool.get_frame();
                if (f) {
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
                // f 出作用域自动归还
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    // 结束后还能继续分配（没有泄漏）
    auto f = pool.get_frame();
    EXPECT_NE(f, nullptr);
}
