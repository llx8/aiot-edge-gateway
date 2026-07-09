#pragma once

#include <string>
#include <sqlite3.h>

class MqttClient;

class OfflineStore {
public:
    explicit OfflineStore(const std::string& db_path);
    ~OfflineStore();

    bool insert(const std::string& topic, const std::string& payload);
    bool flush(MqttClient& mqtt_client);

private:
    sqlite3* db_ = nullptr;
};