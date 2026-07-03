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

// ==================== 构造后 magic 正确 ====================
TEST_F(ShmPublisherTest, Constructor_SetsMagic) {
    ShmPublisher pub(kTestKey);

    int shmid = shmget(kTestKey, sizeof(ShmRegion), 0);
    ASSERT_GE(shmid, 0);

    ShmRegion* region = static_cast<ShmRegion*>(shmat(shmid, nullptr, SHM_RDONLY));
    ASSERT_NE(region, reinterpret_cast<void*>(-1));

    // 两个缓冲区都应该有 magic
    EXPECT_EQ(region->buffers[0].magic, SHM_MAGIC);
    EXPECT_EQ(region->buffers[1].magic, SHM_MAGIC);

    shmdt(region);
}

// ==================== publish 写入数据可读 ====================
TEST_F(ShmPublisherTest, Publish_WritesData) {
    ShmPublisher pub(kTestKey);

    ShmBlock block{};
    block.magic = SHM_MAGIC;
    block.version = 42;  // 这个值会被 publisher 覆盖
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
    int idx = region->active_index;
    const ShmBlock& buf = region->buffers[idx];

    EXPECT_EQ(buf.magic, SHM_MAGIC);
    EXPECT_EQ(buf.version % 2, 0u);  // version 应该是偶数（可读）
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
    block1.magic = SHM_MAGIC;
    block1.total_packets = 10;
    pub.publish(block1);

    ShmBlock block2{};
    block2.magic = SHM_MAGIC;
    block2.total_packets = 20;
    pub.publish(block2);

    int shmid = shmget(kTestKey, sizeof(ShmRegion), 0);
    ShmRegion* region = static_cast<ShmRegion*>(shmat(shmid, nullptr, SHM_RDONLY));

    // 读活跃缓冲区
    int idx = region->active_index;
    const ShmBlock& buf = region->buffers[idx];

    EXPECT_EQ(buf.magic, SHM_MAGIC);
    EXPECT_EQ(buf.version % 2, 0u);  // 偶数（可读）
    EXPECT_EQ(buf.total_packets, 20);

    shmdt(region);
}

// ==================== publish 保证 magic 正确（即使 caller 忘了设） ====================
TEST_F(ShmPublisherTest, Publish_EnsuresMagic) {
    ShmPublisher pub(kTestKey);

    ShmBlock block{};
    block.magic = 0xDEADBEEF;  // 错误值
    pub.publish(block);

    int shmid = shmget(kTestKey, sizeof(ShmRegion), 0);
    ShmRegion* region = static_cast<ShmRegion*>(shmat(shmid, nullptr, SHM_RDONLY));

    // 读活跃缓冲区
    int idx = region->active_index;
    EXPECT_EQ(region->buffers[idx].magic, SHM_MAGIC);  // publish 纠正了

    shmdt(region);
}
