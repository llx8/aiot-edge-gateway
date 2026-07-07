#include <gtest/gtest.h>
#include "ShmPublisher.h"
#include "ShmLayout.h"
#include <sys/shm.h>
#include <cstring>

static constexpr key_t kTestKey = 0x574D5400;

class ShmPublisherTest : public ::testing::Test {
protected:
    void TearDown() override {
        int shmid = shmget(kTestKey, 0, 0);
        if (shmid >= 0) {
            shmctl(shmid, IPC_RMID, nullptr);
        }
    }
};

// ==================== 构造后 read_index 为 0 ====================
TEST_F(ShmPublisherTest, Constructor_InitReadIndex) {
    ShmPublisher pub(kTestKey);

    int shmid = shmget(kTestKey, sizeof(ShmRegion), 0);
    ASSERT_GE(shmid, 0);

    ShmRegion* region = static_cast<ShmRegion*>(shmat(shmid, nullptr, SHM_RDONLY));
    ASSERT_NE(region, reinterpret_cast<void*>(-1));

    // 初始化后 read_index 应该为 0
    EXPECT_EQ(region->read_index.load(std::memory_order_acquire), 0u);

    shmdt(region);
}

// ==================== publish 写入数据可读 ====================
TEST_F(ShmPublisherTest, Publish_WritesData) {
    ShmPublisher pub(kTestKey);

    ShmBlock block{};
    block.total_packets = 100;
    block.total_alarms = 3;
    block.cpu_usage = 0.75f;
    block.mem_usage = 0.50f;
    block.alarm_active = 1;
    strncpy(block.last_alarm, "test_alarm_msg", sizeof(block.last_alarm) - 1);

    pub.publish(block);

    int shmid = shmget(kTestKey, sizeof(ShmRegion), 0);
    ShmRegion* region = static_cast<ShmRegion*>(shmat(shmid, nullptr, SHM_RDONLY));

    // 读活跃缓冲区
    int idx = region->read_index.load(std::memory_order_acquire);
    const ShmBlock& buf = region->buffers[idx];

    EXPECT_EQ(buf.total_packets, 100);
    EXPECT_EQ(buf.total_alarms, 3);
    EXPECT_FLOAT_EQ(buf.cpu_usage, 0.75f);
    EXPECT_FLOAT_EQ(buf.mem_usage, 0.50f);
    EXPECT_EQ(buf.alarm_active, 1);
    EXPECT_STREQ(buf.last_alarm, "test_alarm_msg");

    shmdt(region);
}

// ==================== publish 覆盖旧数据 ====================
TEST_F(ShmPublisherTest, Publish_OverwritesOldData) {
    ShmPublisher pub(kTestKey);

    ShmBlock block1{};
    block1.total_packets = 10;
    pub.publish(block1);

    ShmBlock block2{};
    block2.total_packets = 20;
    pub.publish(block2);

    int shmid = shmget(kTestKey, sizeof(ShmRegion), 0);
    ShmRegion* region = static_cast<ShmRegion*>(shmat(shmid, nullptr, SHM_RDONLY));

    // 读活跃缓冲区
    int idx = region->read_index.load(std::memory_order_acquire);
    const ShmBlock& buf = region->buffers[idx];

    EXPECT_EQ(buf.total_packets, 20);

    shmdt(region);
}

// ==================== publish 切换 read_index ====================
TEST_F(ShmPublisherTest, Publish_SwitchesReadIndex) {
    ShmPublisher pub(kTestKey);

    ShmBlock block{};
    block.total_packets = 1;

    // 第一次发布：read_index 从 0 切到 1
    pub.publish(block);
    int shmid = shmget(kTestKey, sizeof(ShmRegion), 0);
    ShmRegion* region = static_cast<ShmRegion*>(shmat(shmid, nullptr, SHM_RDONLY));
    EXPECT_EQ(region->read_index.load(std::memory_order_acquire), 1u);

    // 第二次发布：read_index 从 1 切回 0
    block.total_packets = 2;
    pub.publish(block);
    EXPECT_EQ(region->read_index.load(std::memory_order_acquire), 0u);

    // 验证数据正确
    EXPECT_EQ(region->buffers[0].total_packets, 2);

    shmdt(region);
}
