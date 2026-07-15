#pragma once

#include <string>
#include <sqlite3.h>

class MqttClient;

class OfflineStore {
public:
    explicit OfflineStore(const std::string& db_path);
    ~OfflineStore();

    // priority: 0=周期数据, 1=告警（值越大越优先补传）
    bool insert(const std::string& topic, const std::string& payload, int priority = 0);
    bool flush(MqttClient& mqtt_client);

private:
    sqlite3* db_ = nullptr;
};