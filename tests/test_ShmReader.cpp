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
    block.version = 10;
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

    EXPECT_EQ(out.version, 10ul);
    EXPECT_EQ(out.total_packets, 200);
    EXPECT_EQ(out.total_alarms, 5);
    EXPECT_FLOAT_EQ(out.cpu_usage, 0.65f);
    EXPECT_FLOAT_EQ(out.mem_usage, 0.40f);
    EXPECT_EQ(out.online_nodes, 8);
    EXPECT_EQ(out.alarm_active, 1);
    EXPECT_STREQ(out.last_alarm, "over_temp");
}

// ==================== version 更新后 has_new_data 返回 true ====================
TEST_F(ShmReaderTest, HasNewData_DetectsVersionChange) {
    ShmPublisher pub(kTestKey);

    ShmBlock block1{};
    block1.magic = SHM_MAGIC;
    block1.version = 1;
    pub.publish(block1);

    ShmReader reader(kTestKey);
    ShmBlock out{};
    reader.read(out);

    EXPECT_FALSE(reader.has_new_data());

    ShmBlock block2{};
    block2.magic = SHM_MAGIC;
    block2.version = 2;
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

    int shmid = shmget(kTestKey, sizeof(ShmBlock), 0666);
    ASSERT_GE(shmid, 0);
    ShmBlock* raw = static_cast<ShmBlock*>(shmat(shmid, nullptr, 0));
    ASSERT_NE(raw, reinterpret_cast<void*>(-1));

    raw->magic = 0xDEADBEEF;
    shmdt(raw);

    ShmReader reader(kTestKey);
    EXPECT_FALSE(reader.read(out));
    EXPECT_FALSE(reader.has_new_data());
}

// ==================== read 返回的是拷贝，修改不影响共享内存 ====================
TEST_F(ShmReaderTest, Read_ReturnsCopyNotReference) {
    ShmPublisher pub(kTestKey);

    ShmBlock block{};
    block.magic = SHM_MAGIC;
    block.version = 1;
    block.total_packets = 50;
    pub.publish(block);

    ShmReader reader(kTestKey);
    ShmBlock out{};
    ASSERT_TRUE(reader.read(out));

    out.total_packets = 999;
    out.version = 999;

    ShmBlock out2{};
    ASSERT_TRUE(reader.read(out2));
    EXPECT_EQ(out2.total_packets, 50);
    EXPECT_EQ(out2.version, 1ul);
}