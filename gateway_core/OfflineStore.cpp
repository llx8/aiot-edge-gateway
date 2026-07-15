#include "OfflineStore.h"
#include "MqttClient.h"
#include "Logger.h"

OfflineStore::OfflineStore(const std::string& db_path) {
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        GetLogger("OfflineStore")->error("open failed: {}", sqlite3_errmsg(db_));
        db_ = nullptr;
        return;
    }
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS offline_queue (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            topic TEXT NOT NULL,
            payload TEXT NOT NULL,
            priority INTEGER NOT NULL DEFAULT 0
        );
    )";
    sqlite3_exec(db_, sql, nullptr, nullptr, nullptr);
}

OfflineStore::~OfflineStore() {
    if (db_) sqlite3_close(db_);
}

bool OfflineStore::insert(const std::string& topic, const std::string& payload, int priority) {
    if (!db_) return false;

    const char* sql = "INSERT INTO offline_queue (topic, payload, priority) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, topic.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, payload.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, priority);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool OfflineStore::flush(MqttClient& mqtt_client) {
    if (!db_) return false;

    const char* select_sql = "SELECT id, topic, payload FROM offline_queue ORDER BY priority DESC, id ASC;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, select_sql, -1, &stmt, nullptr);

    int flushed = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int64_t id = sqlite3_column_int64(stmt, 0);
        std::string topic = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        std::string payload = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

        if (mqtt_client.publish(topic, payload)) {
            // 成功 → 删这条
            sqlite3_stmt* del_stmt = nullptr;
            sqlite3_prepare_v2(db_, "DELETE FROM offline_queue WHERE id = ?;", -1, &del_stmt, nullptr);
            sqlite3_bind_int64(del_stmt, 1, id);
            sqlite3_step(del_stmt);
            sqlite3_finalize(del_stmt);
            flushed++;
        } else {
            // 失败 → 停下
            GetLogger("OfflineStore")->warn("publish failed during flush, stopping. flushed={}", flushed);
            break;
        }
    }
    sqlite3_finalize(stmt);
    GetLogger("OfflineStore")->info("flush complete: {} records sent", flushed);
    return flushed > 0;
}