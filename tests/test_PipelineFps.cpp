#include <gtest/gtest.h>
#include "pipeline/Pipeline.h"

using namespace gateway_engine;

// Pipeline 帧率统计测试
TEST(PipelineFpsTest, FrameCountAfterOnFrameDone) {
    PipelineConfig cfg;
    cfg.input_size = 640;
    cfg.frame_pool_size = 6;
    cfg.queue_capacity = 4;

    Pipeline pipeline(cfg);

    // 初始 fps 应为 0
    EXPECT_FLOAT_EQ(pipeline.fps(), 0.0f);

    // 模拟 5 帧完成
    for (int i = 0; i < 5; i++) {
        pipeline.on_frame_done();
    }

    // tick 后 fps 应为 5
    pipeline.tick_fps();
    EXPECT_FLOAT_EQ(pipeline.fps(), 5.0f);

    // 再模拟 3 帧
    for (int i = 0; i < 3; i++) {
        pipeline.on_frame_done();
    }

    // tick 后 fps 应为 3
    pipeline.tick_fps();
    EXPECT_FLOAT_EQ(pipeline.fps(), 3.0f);
}

// 验证 frame_count_ 被重置（连续两次 tick，第二次应为 0）
TEST(PipelineFpsTest, TickResetsFrameCount) {
    PipelineConfig cfg;
    Pipeline pipeline(cfg);

    pipeline.on_frame_done();
    pipeline.tick_fps();
    EXPECT_FLOAT_EQ(pipeline.fps(), 1.0f);

    // 没有新帧，tick 后应为 0
    pipeline.tick_fps();
    EXPECT_FLOAT_EQ(pipeline.fps(), 0.0f);
}

// fps() 返回值在 tick 之间保持不变
TEST(PipelineFpsTest, FpsStableBetweenTicks) {
    PipelineConfig cfg;
    Pipeline pipeline(cfg);

    pipeline.on_frame_done();
    pipeline.on_frame_done();
    pipeline.tick_fps();
    EXPECT_FLOAT_EQ(pipeline.fps(), 2.0f);

    // 又来了新帧，但还没 tick，fps 应保持上一个值
    pipeline.on_frame_done();
    pipeline.on_frame_done();
    pipeline.on_frame_done();
    EXPECT_FLOAT_EQ(pipeline.fps(), 2.0f);
}

// avg_latency_ms：单帧延迟为 0（单帧无法计算间隔）
TEST(PipelineFpsTest, AvgLatencySingleFrame) {
    PipelineConfig cfg;
    Pipeline pipeline(cfg);
    pipeline.on_frame_done();
    EXPECT_FLOAT_EQ(pipeline.avg_latency_ms(), 0.0f);
}

// avg_latency_ms：多帧后延迟应 > 0
TEST(PipelineFpsTest, AvgLatencyMultipleFrames) {
    PipelineConfig cfg;
    Pipeline pipeline(cfg);
    for (int i = 0; i < 5; ++i) {
        pipeline.on_frame_done();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_GT(pipeline.avg_latency_ms(), 0.0f);
}

// avg_latency_ms：窗口上限 30 帧，超过不崩
TEST(PipelineFpsTest, AvgLatencyWindowLimit) {
    PipelineConfig cfg;
    Pipeline pipeline(cfg);
    for (int i = 0; i < 100; ++i) {
        pipeline.on_frame_done();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // 100 帧后 avg_latency 应为正值
    EXPECT_GT(pipeline.avg_latency_ms(), 0.0f);
}
