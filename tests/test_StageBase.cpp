#include <gtest/gtest.h>
#include "pipeline/stages/StageBase.h"
#include <atomic>
#include <chrono>
#include <thread>

using namespace gateway_engine;

// 一个简单的 Stage 实现用于测试基类
class TestStage : public StageBase {
public:
    std::atomic<bool> has_exited{false};
protected:
    void run() override {
        while (running_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        has_exited = true;
    }
};

// start/stop 基本生命周期：stop 后线程退出
TEST(StageBaseTest, StartStop) {
    TestStage stage;
    stage.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    stage.stop();
    EXPECT_TRUE(stage.has_exited);
}

// Stop 在 Start 之前调用，不应崩溃
TEST(StageBaseTest, StopBeforeStart) {
    TestStage stage;
    stage.stop();   // 还没 start，应安全
    SUCCEED();
}

// 重复 stop 不应崩溃
TEST(StageBaseTest, DoubleStop) {
    TestStage stage;
    stage.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    stage.stop();
    stage.stop();   // 第二次 stop，应安全
    SUCCEED();
}
