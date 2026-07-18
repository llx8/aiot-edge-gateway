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
    // WAL 模式 + busy_timeout 3s：与 DbWriter 并发写时避免 SQLITE_BUSY 静默丢数据
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_busy_timeout(db_, 3000);

    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS offline_cache (
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

    const char* sql = "INSERT INTO offline_cache (topic, payload, priority) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        GetLogger("OfflineStore")->error("insert prepare failed: {}", sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_bind_text(stmt, 1, topic.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, payload.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, priority);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool OfflineStore::flush(MqttClient& mqtt_client) {
    if (!db_) return false;

    const char* select_sql = "SELECT id, topic, payload FROM offline_cache ORDER BY priority DESC, id ASC;";
    sqlite3_stmt* stmt = nullptr;
    int select_rc = sqlite3_prepare_v2(db_, select_sql, -1, &stmt, nullptr);
    if (select_rc != SQLITE_OK) {
        GetLogger("OfflineStore")->error("flush select prepare failed: {}", sqlite3_errmsg(db_));
        return false;
    }

    int flushed = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int64_t id = sqlite3_column_int64(stmt, 0);
        const char* topic_cstr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* payload_cstr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        std::string topic = topic_cstr ? topic_cstr : "";
        std::string payload = payload_cstr ? payload_cstr : "";

        if (mqtt_client.publish(topic, payload)) {
            // 成功 → 删这条
            sqlite3_stmt* del_stmt = nullptr;
            int del_rc = sqlite3_prepare_v2(db_, "DELETE FROM offline_cache WHERE id = ?;", -1, &del_stmt, nullptr);
            if (del_rc != SQLITE_OK) {
                GetLogger("OfflineStore")->error("flush delete prepare failed: {}", sqlite3_errmsg(db_));
                continue;
            }
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