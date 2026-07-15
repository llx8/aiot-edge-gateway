#include <gtest/gtest.h>
#include "DbWriter.h"
#include <sqlite3.h>
#include <cstdio>
#include <string>

// DbWriter 为异步落盘：push 进 SPSC 队列，DB 线程批量写。
// 测试关键点：push 后必须 stop()，stop() 会在 DB 线程退出前排空队列（见 loop() 末尾 drain），
// 这样才能保证数据已落盘再查询。
class DbWriterTest : public ::testing::Test {
protected:
    static constexpr const char* kTestDb = "/tmp/test_gateway_core.db";

    void SetUp() override {
        std::remove(kTestDb);
        std::remove("/tmp/test_gateway_core.db-wal");
        std::remove("/tmp/test_gateway_core.db-shm");
    }

    void TearDown() override {
        std::remove(kTestDb);
        std::remove("/tmp/test_gateway_core.db-wal");
        std::remove("/tmp/test_gateway_core.db-shm");
    }

    static bool row_exists(sqlite3* db, const char* sql) {
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_ROW;
    }
};

// ==================== 数据库创建 ====================
TEST_F(DbWriterTest, Constructor_CreatesTables) {
    {
        DbWriter writer(kTestDb);  // 构造即建表，未启动线程
    }  // 析构 -> stop()（未 start 时为空操作）-> 关闭 db

    sqlite3* db = nullptr;
    sqlite3_open(kTestDb, &db);
    ASSERT_NE(db, nullptr);

    EXPECT_TRUE(row_exists(db,
        "SELECT name FROM sqlite_master WHERE type='table' AND name='sensor_data';"));
    EXPECT_TRUE(row_exists(db,
        "SELECT name FROM sqlite_master WHERE type='table' AND name='alarm_log';"));

    sqlite3_close(db);
}

// ==================== push 传感器数据返回 true ====================
TEST_F(DbWriterTest, PushSensor_ReturnsTrue) {
    DbWriter writer(kTestDb);
    writer.start();

    bool ok = writer.push({DbOpType::SENSOR, 0, 1, 0x01, "temp:25.5,humidity:60.2"});
    EXPECT_TRUE(ok);

    writer.stop();  // 排空队列，确保落盘
}

// ==================== 传感器数据已落盘 ====================
TEST_F(DbWriterTest, PushSensor_DataPersisted) {
    DbWriter writer(kTestDb);
    writer.start();

    writer.push({DbOpType::SENSOR, 0, 1, 0x01, "temp:25.5"});
    writer.push({DbOpType::SENSOR, 0, 2, 0xFF, "heartbeat"});
    writer.stop();  // 同步等待 DB 线程把队列排空

    sqlite3* db = nullptr;
    sqlite3_open(kTestDb, &db);

    {
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db,
            "SELECT source_type, node_id, tlv_type, value FROM sensor_data WHERE node_id = 1;",
            -1, &stmt, nullptr);
        ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
        EXPECT_EQ(sqlite3_column_int(stmt, 0), 0);
        EXPECT_EQ(sqlite3_column_int(stmt, 1), 1);
        EXPECT_EQ(sqlite3_column_int(stmt, 2), 0x01);
        EXPECT_EQ(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))), "temp:25.5");
        sqlite3_finalize(stmt);
    }

    {
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db,
            "SELECT source_type, node_id, tlv_type, value FROM sensor_data WHERE node_id = 2;",
            -1, &stmt, nullptr);
        ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
        EXPECT_EQ(sqlite3_column_int(stmt, 0), 0);
        EXPECT_EQ(sqlite3_column_int(stmt, 1), 2);
        EXPECT_EQ(sqlite3_column_int(stmt, 2), 0xFF);
        EXPECT_EQ(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))), "heartbeat");
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
}

// ==================== 告警数据已落盘 ====================
TEST_F(DbWriterTest, PushAlarm_DataPersisted) {
    DbWriter writer(kTestDb);
    writer.start();

    writer.push({DbOpType::ALARM, 0, 1, 0x04, "温度超过80度: 85.3"});
    writer.stop();

    sqlite3* db = nullptr;
    sqlite3_open(kTestDb, &db);

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db,
        "SELECT source_type, node_id, tlv_type, detail FROM alarm_log WHERE node_id = 1;",
        -1, &stmt, nullptr);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int(stmt, 0), 0);
    EXPECT_EQ(sqlite3_column_int(stmt, 1), 1);
    EXPECT_EQ(sqlite3_column_int(stmt, 2), 0x04);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))),
              "温度超过80度: 85.3");
    sqlite3_finalize(stmt);

    sqlite3_close(db);
}

// ==================== 重复打开数据库，数据累计不丢失 ====================
TEST_F(DbWriterTest, ReopenSameDatabase_DataAccumulates) {
    {
        DbWriter w1(kTestDb);
        w1.start();
        w1.push({DbOpType::SENSOR, 0, 1, 0x01, "data1"});
        w1.stop();  // 必须先排空再析构
    }
    {
        DbWriter w2(kTestDb);
        w2.start();
        w2.push({DbOpType::SENSOR, 0, 2, 0x02, "data2"});
        w2.stop();
    }

    sqlite3* db = nullptr;
    sqlite3_open(kTestDb, &db);

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM sensor_data;", -1, &stmt, nullptr);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int(stmt, 0), 2);
    sqlite3_finalize(stmt);

    sqlite3_close(db);
}
