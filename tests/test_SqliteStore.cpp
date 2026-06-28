#include <gtest/gtest.h>
#include "SqliteStore.h"
#include <sqlite3.h>
#include <cstdio>
#include <string>

class SqliteStoreTest : public ::testing::Test {
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
TEST_F(SqliteStoreTest, Constructor_CreatesTables) {
    {
        SqliteStore store(kTestDb);
    }  // store 析构，触发 WAL checkpoint

    sqlite3* db = nullptr;
    sqlite3_open(kTestDb, &db);
    ASSERT_NE(db, nullptr);

    // 验证 sensor_data 表存在
    EXPECT_TRUE(row_exists(db,
        "SELECT name FROM sqlite_master WHERE type='table' AND name='sensor_data';"
    ));
    // 验证 alarm_log 表存在
    EXPECT_TRUE(row_exists(db,
        "SELECT name FROM sqlite_master WHERE type='table' AND name='alarm_log';"
    ));

    sqlite3_close(db);
}

// ==================== 插入传感器数据 ====================
TEST_F(SqliteStoreTest, InsertSensor_ReturnsTrue) {
    SqliteStore store(kTestDb);

    bool ok = store.insert_sensor(1000000, SourceType::TCP_SENSOR, "temp:25.5,humidity:60.2");
    EXPECT_TRUE(ok);
}

// ==================== 验证传感器数据已写入 ====================
TEST_F(SqliteStoreTest, InsertSensor_DataPersisted) {
    SqliteStore store(kTestDb);

    store.insert_sensor(1000000, SourceType::TCP_SENSOR, "temp:25.5");
    store.insert_sensor(2000000, static_cast<SourceType>(0xFF), "heartbeat");

    sqlite3* db = nullptr;
    sqlite3_open(kTestDb, &db);

    // 查第一条
    {
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db,
            "SELECT ts, sensor_type, value FROM sensor_data WHERE ts = 1000000;",
            -1, &stmt, nullptr);
        ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
        EXPECT_EQ(sqlite3_column_int64(stmt, 0), 1000000);
        EXPECT_EQ(sqlite3_column_int(stmt, 1), static_cast<int>(SourceType::TCP_SENSOR));
        EXPECT_EQ(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))), "temp:25.5");
        sqlite3_finalize(stmt);
    }

    // 查第二条
    {
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db,
            "SELECT ts, sensor_type, value FROM sensor_data WHERE ts = 2000000;",
            -1, &stmt, nullptr);
        ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
        EXPECT_EQ(sqlite3_column_int64(stmt, 0), 2000000);
        EXPECT_EQ(sqlite3_column_int(stmt, 1), 0xFF);
        EXPECT_EQ(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))), "heartbeat");
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
}

// ==================== 插入告警 ====================
TEST_F(SqliteStoreTest, InsertAlarm_DataPersisted) {
    SqliteStore store(kTestDb);

    store.insert_alarm(3000000, static_cast<SourceType>(0x04), "温度超过80度: 85.3");

    sqlite3* db = nullptr;
    sqlite3_open(kTestDb, &db);

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db,
        "SELECT ts, alarm_type, detail FROM alarm_log WHERE ts = 3000000;",
        -1, &stmt, nullptr);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int64(stmt, 0), 3000000);
    EXPECT_EQ(sqlite3_column_int(stmt, 1), 0x04);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))),
              "温度超过80度: 85.3");
    sqlite3_finalize(stmt);

    sqlite3_close(db);
}

// ==================== 重复打开数据库不崩溃 ====================
TEST_F(SqliteStoreTest, ReopenSameDatabase_DoesNotCrash) {
    {
        SqliteStore store1(kTestDb);
        store1.insert_sensor(1, SourceType::TCP_SENSOR, "data1");
    }
    {
        SqliteStore store2(kTestDb);
        store2.insert_sensor(2, static_cast<SourceType>(0x02), "data2");
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
