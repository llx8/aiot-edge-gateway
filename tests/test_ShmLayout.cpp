#include <gtest/gtest.h>
#include "ShmLayout.h"

// ========== 结构体大小和对齐 ==========

TEST(ShmLayoutTest, StructSize) {
    // magic(4) + version(8) + uptime_sec(8) + total_packets(4) + total_alarms(4)
    // + cpu_usage(4) + mem_usage(4) + online_nodes(4) + mqtt_connected(4)
    // + alarm_active(4) + last_alarm(128)
    // = 8+8+8+4+4+4+4+4+4+4+128 = 180
    EXPECT_EQ(sizeof(ShmBlock), 176u);

    // 在共享内存 IPC 中，这个值跨进程必须一致，写死在这里防止意外变化
}

// 魔数值必须正确
TEST(ShmLayoutTest, MagicValue) {
    EXPECT_EQ(SHM_MAGIC, 0x47574D4Du);
}

// last_alarm 字段可写入和读取
TEST(ShmLayoutTest, LastAlarmField) {
    ShmBlock block = {};
    block.magic = SHM_MAGIC;

    const char* msg = "节点3高温: 86°C";
    strncpy(block.last_alarm, msg, sizeof(block.last_alarm) - 1);

    EXPECT_STREQ(block.last_alarm, msg);
}

// version 递增一致性（模拟 SeqLock 场景）
TEST(ShmLayoutTest, VersionIncrement) {
    ShmBlock block = {};
    block.magic = SHM_MAGIC;

    uint64_t v1 = block.version;
    block.version++;
    uint64_t v2 = block.version;

    EXPECT_EQ(v2, v1 + 1);
}
