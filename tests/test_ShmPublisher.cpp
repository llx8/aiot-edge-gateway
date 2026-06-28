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

    int shmid = shmget(kTestKey, sizeof(ShmBlock), 0);
    ASSERT_GE(shmid, 0);

    ShmBlock* ptr = static_cast<ShmBlock*>(shmat(shmid, nullptr, SHM_RDONLY));
    ASSERT_NE(ptr, reinterpret_cast<void*>(-1));

    EXPECT_EQ(ptr->magic, SHM_MAGIC);

    shmdt(ptr);
}

// ==================== publish 写入数据可读 ====================
TEST_F(ShmPublisherTest, Publish_WritesData) {
    ShmPublisher pub(kTestKey);

    ShmBlock block{};
    block.magic = SHM_MAGIC;
    block.version = 42;
    block.total_packets = 100;
    block.total_alarms = 3;
    block.cpu_usage = 0.75f;
    block.mem_usage = 0.50f;
    block.alarm_active = 1;
    strncpy(block.last_alarm, "test_alarm_msg", sizeof(block.last_alarm) - 1);

    pub.publish(block);

    int shmid = shmget(kTestKey, sizeof(ShmBlock), 0);
    ShmBlock* ptr = static_cast<ShmBlock*>(shmat(shmid, nullptr, SHM_RDONLY));

    EXPECT_EQ(ptr->magic, SHM_MAGIC);
    EXPECT_EQ(ptr->version, 42ul);
    EXPECT_EQ(ptr->total_packets, 100);
    EXPECT_EQ(ptr->total_alarms, 3);
    EXPECT_FLOAT_EQ(ptr->cpu_usage, 0.75f);
    EXPECT_FLOAT_EQ(ptr->mem_usage, 0.50f);
    EXPECT_EQ(ptr->alarm_active, 1);
    EXPECT_STREQ(ptr->last_alarm, "test_alarm_msg");

    shmdt(ptr);
}

// ==================== publish 覆盖旧数据 ====================
TEST_F(ShmPublisherTest, Publish_OverwritesOldData) {
    ShmPublisher pub(kTestKey);

    ShmBlock block1{};
    block1.magic = SHM_MAGIC;
    block1.version = 1;
    block1.total_packets = 10;
    pub.publish(block1);

    ShmBlock block2{};
    block2.magic = SHM_MAGIC;
    block2.version = 2;
    block2.total_packets = 20;
    pub.publish(block2);

    int shmid = shmget(kTestKey, sizeof(ShmBlock), 0);
    ShmBlock* ptr = static_cast<ShmBlock*>(shmat(shmid, nullptr, SHM_RDONLY));

    EXPECT_EQ(ptr->version, 2ul);
    EXPECT_EQ(ptr->total_packets, 20);

    shmdt(ptr);
}

// ==================== publish 保证 magic 正确（即使 caller 忘了设） ====================
TEST_F(ShmPublisherTest, Publish_EnsuresMagic) {
    ShmPublisher pub(kTestKey);

    ShmBlock block{};
    block.magic = 0xDEADBEEF;  // 错误值
    pub.publish(block);

    int shmid = shmget(kTestKey, sizeof(ShmBlock), 0);
    ShmBlock* ptr = static_cast<ShmBlock*>(shmat(shmid, nullptr, SHM_RDONLY));

    EXPECT_EQ(ptr->magic, SHM_MAGIC);  // publish 纠正了

    shmdt(ptr);
}
