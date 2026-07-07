#include <gtest/gtest.h>
#include "ShmReader.h"
#include "ShmPublisher.h"
#include "ShmLayout.h"
#include <sys/shm.h>
#include <cstring>

static constexpr key_t kTestKey = 0x574D5401;

class ShmReaderTest : public ::testing::Test {
protected:
    void TearDown() override {
        int shmid = shmget(kTestKey, 0, 0);
        if (shmid >= 0) {
            shmctl(shmid, IPC_RMID, nullptr);
        }
    }
};

// ==================== 读到进程 B 写入的数据 ====================
TEST_F(ShmReaderTest, Read_GetsPublisherData) {
    ShmPublisher pub(kTestKey);

    ShmBlock block{};
    block.magic = SHM_MAGIC;
    block.total_packets = 200;
    block.total_alarms = 5;
    block.cpu_usage = 0.65f;
    block.mem_usage = 0.40f;
    block.online_nodes = 8;
    block.alarm_active = 1;
    strncpy(block.last_alarm, "over_temp", sizeof(block.last_alarm) - 1);

    pub.publish(block);

    ShmReader reader(kTestKey);
    ShmBlock out{};
    ASSERT_TRUE(reader.read(out));

    EXPECT_EQ(out.magic, SHM_MAGIC);
    EXPECT_EQ(out.version % 2, 0u);  // version 应该是偶数（可读）
    EXPECT_EQ(out.total_packets, 200);
    EXPECT_EQ(out.total_alarms, 5);
    EXPECT_FLOAT_EQ(out.cpu_usage, 0.65f);
    EXPECT_FLOAT_EQ(out.mem_usage, 0.40f);
    EXPECT_EQ(out.online_nodes, 8);
    EXPECT_EQ(out.alarm_active, 1);
    EXPECT_STREQ(out.last_alarm, "over_temp");
}

// ==================== 索引更新后 has_new_data 返回 true ====================
TEST_F(ShmReaderTest, HasNewData_DetectsIndexChange) {
    ShmPublisher pub(kTestKey);

    ShmBlock block1{};
    block1.magic = SHM_MAGIC;
    pub.publish(block1);

    ShmReader reader(kTestKey);
    ShmBlock out{};
    reader.read(out);

    EXPECT_FALSE(reader.has_new_data());

    // 发布一次，read_index 切换到另一个 buffer
    ShmBlock block2{};
    block2.magic = SHM_MAGIC;
    block2.total_packets = 42;
    pub.publish(block2);

    EXPECT_TRUE(reader.has_new_data());
}

// ==================== 共享内存不存在时 read 返回 false ====================
TEST_F(ShmReaderTest, Read_ReturnsFalse_WhenShmNotExist) {
    key_t bad_key = 0x574D5499;

    ShmReader reader(bad_key);
    ShmBlock out{};
    EXPECT_FALSE(reader.read(out));
    EXPECT_FALSE(reader.has_new_data());
}

// ==================== magic 不正确时 read 返回 false ====================
TEST_F(ShmReaderTest, Read_ReturnsFalse_WhenMagicMismatch) {
    ShmPublisher pub(kTestKey);

    ShmBlock out{};

    int shmid = shmget(kTestKey, sizeof(ShmRegion), 0666);
    ASSERT_GE(shmid, 0);
    ShmRegion* region = static_cast<ShmRegion*>(shmat(shmid, nullptr, 0));
    ASSERT_NE(region, reinterpret_cast<void*>(-1));

    // 破坏两个缓冲区的 magic
    region->buffers[0].magic = 0xDEADBEEF;
    region->buffers[1].magic = 0xDEADBEEF;
    shmdt(region);

    ShmReader reader(kTestKey);
    EXPECT_FALSE(reader.read(out));
    EXPECT_FALSE(reader.has_new_data());
}

// ==================== read 返回的是拷贝，修改不影响共享内存 ====================
TEST_F(ShmReaderTest, Read_ReturnsCopyNotReference) {
    ShmPublisher pub(kTestKey);

    ShmBlock block{};
    block.magic = SHM_MAGIC;
    block.total_packets = 50;
    pub.publish(block);

    ShmReader reader(kTestKey);
    ShmBlock out{};
    ASSERT_TRUE(reader.read(out));

    // 修改读到的数据
    out.total_packets = 999;
    out.version = 999;

    // 再次发布新数据（发布两次，确保有新数据）
    ShmBlock block2{};
    block2.magic = SHM_MAGIC;
    block2.total_packets = 50;
    pub.publish(block2);
    pub.publish(block2);

    // 再次读取，应该还是原始数据
    ShmBlock out2{};
    ASSERT_TRUE(reader.read(out2));
    EXPECT_EQ(out2.total_packets, 50);
    EXPECT_EQ(out2.version % 2, 0u);  // 偶数（可读）
}
