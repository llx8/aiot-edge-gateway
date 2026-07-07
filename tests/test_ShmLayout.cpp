#include <gtest/gtest.h>
#include "ShmLayout.h"

// ========== 结构体大小和对齐 ==========

TEST(ShmLayoutTest, StructSize) {
    // ShmBlock 包含：基础指标 + AI指标 + 快照信号 + reserved
    // 具体大小取决于编译器对齐，确保大于0且合理
    EXPECT_GT(sizeof(ShmBlock), 0u);

    // ShmRegion = 2个ShmBlock + atomic<uint32_t>
    EXPECT_GT(sizeof(ShmRegion), sizeof(ShmBlock) * 2);
}

// 魔数值必须正确
TEST(ShmLayoutTest, MagicValue) {
    EXPECT_EQ(SHM_MAGIC, 0x47574D4Du);
}

// last_alarm 字段可写入和读取
TEST(ShmLayoutTest, LastAlarmField) {
    ShmBlock block = {};

    const char* msg = "节点3高温: 86°C";
    strncpy(block.last_alarm, msg, sizeof(block.last_alarm) - 1);

    EXPECT_STREQ(block.last_alarm, msg);
}
