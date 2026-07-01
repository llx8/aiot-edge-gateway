#pragma once

#include <sqlite3.h>
#include <string>
#include <cstdint>

class SqliteStore {
public:
    explicit SqliteStore(const std::string& db_path);
    ~SqliteStore();

    // 插入传感器数据
    bool insert_sensor(int32_t source_type, int32_t node_id, uint8_t tlv_type, const std::string& value);

    // 插入告警
    bool insert_alarm(int32_t source_type, int32_t node_id, uint8_t tlv_type, const std::string& detail);

private:
    sqlite3* db_;

    void create_table();
};