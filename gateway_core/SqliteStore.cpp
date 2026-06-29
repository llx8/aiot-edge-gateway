#include "SqliteStore.h"
#include <stdexcept>

// 构造函数
SqliteStore::SqliteStore(const std::string& db_path) 
    : db_(nullptr)
    {
        int rc = sqlite3_open(db_path.c_str(), &db_);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("Cannot open database: " + std::string(sqlite3_errmsg(db_)));
        }
        sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
        create_table();
    }

// 析构函数
SqliteStore::~SqliteStore() {
    if (db_) {
        sqlite3_close(db_);
    }
}

// 创建表
void SqliteStore::create_table() {
    const char* sql_sensor = R"(
        CREATE TABLE IF NOT EXISTS sensor_data (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            source_type INTEGER NOT NULL,
            node_id INTEGER NOT NULL,
            tlv_type INTEGER NOT NULL,
            value TEXT NOT NULL
        );
    )";
    const char* sql_alarm = R"(
        CREATE TABLE IF NOT EXISTS alarm_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            source_type INTEGER NOT NULL,
            node_id INTEGER NOT NULL,
            tlv_type INTEGER NOT NULL,
            detail TEXT NOT NULL
        );
    )";

    sqlite3_exec(db_, sql_sensor, nullptr, nullptr, nullptr);
    sqlite3_exec(db_, sql_alarm, nullptr, nullptr, nullptr);
}

// 插入传感器数据
bool SqliteStore::insert_sensor(int32_t source_type, int32_t node_id, uint8_t tlv_type, const std::string& value){
    const char* sql = "INSERT INTO sensor_data (source_type, node_id, tlv_type, value) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    sqlite3_bind_int(stmt, 1, static_cast<int>(source_type));
    sqlite3_bind_int(stmt, 2, static_cast<int>(node_id));
    sqlite3_bind_int(stmt, 3, static_cast<int>(tlv_type));
    sqlite3_bind_text(stmt, 4, value.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

// 插入告警
bool SqliteStore::insert_alarm(int32_t source_type, int32_t node_id, uint8_t tlv_type, const std::string& detail){
    const char* sql = "INSERT INTO alarm_log (source_type, node_id, tlv_type, detail) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    sqlite3_bind_int(stmt, 1, static_cast<int>(source_type));
    sqlite3_bind_int(stmt, 2, static_cast<int>(node_id));
    sqlite3_bind_int(stmt, 3, static_cast<int>(tlv_type));
    sqlite3_bind_text(stmt, 4, detail.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}