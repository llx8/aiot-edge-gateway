#pragma once

#include <sqlite3.h>
#include <string>
#include <cstdint>
#include "InternalMessage.h"

class SqliteStore {
public:
    explicit SqliteStore(const std::string& db_path);
    ~SqliteStore();

    // 插入传感器数据
    bool insert_sensor(int64_t ts, SourceType sensor_type, const std::string& value);

    // 插入告警
    bool insert_alarm(int64_t ts, SourceType alarm_type, const std::string& detail);

private:
    sqlite3* db_;

    void create_table();
};