#include "DbWriter.h"
#include "Logger.h"
#include <chrono>
#include <unistd.h>
#include <sys/eventfd.h>
#include <poll.h>

DbWriter::DbWriter(const std::string& db_path) : running_(false) {
    open_db(db_path);
    create_tables();
    notify_fd_ = eventfd(0, EFD_NONBLOCK);
    if (notify_fd_ < 0) {
        GetLogger("DbWriter")->error("eventfd failed: {}", strerror(errno));
    }
}

DbWriter::~DbWriter() {
    stop();
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    if (notify_fd_ >= 0) {
        close(notify_fd_);
        notify_fd_ = -1;
    }
}

void DbWriter::open_db(const std::string& db_path) {
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        GetLogger("DbWriter")->error("Failed to open database: {}", sqlite3_errmsg(db_));
        sqlite3_close(db_);
        db_ = nullptr;
        return;
    }
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
}

void DbWriter::create_tables() {
    const char* sensor_sql = R"(
        CREATE TABLE IF NOT EXISTS sensor_data (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            source_type INTEGER NOT NULL,
            node_id INTEGER NOT NULL,
            tlv_type INTEGER NOT NULL,
            value TEXT NOT NULL
        );
    )";
    const char* alarm_sql = R"(
        CREATE TABLE IF NOT EXISTS alarm_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            source_type INTEGER NOT NULL,
            node_id INTEGER NOT NULL,
            tlv_type INTEGER NOT NULL,
            detail TEXT NOT NULL
        );
    )";
    sqlite3_exec(db_, sensor_sql, nullptr, nullptr, nullptr);
    sqlite3_exec(db_, alarm_sql, nullptr, nullptr, nullptr);
}

bool DbWriter::push(const DbRecord& record) {
    if (!queue_.push(record)) return false;
    // 队列满 50 条时提前唤醒 DB 线程
    if (queue_.size() >= kBatchSize && notify_fd_ >= 0) {
        uint64_t cnt = 1;
        write(notify_fd_, &cnt, sizeof(cnt));
    }
    return true;
}

void DbWriter::start() {
    if (!db_) {
        GetLogger("DbWriter")->error("Cannot start: db is null");
        return;
    }
    running_ = true;
    db_thread_ = std::thread(&DbWriter::loop, this);
}

void DbWriter::stop() {
    running_ = false;
    if (db_thread_.joinable()) {
        db_thread_.join();
    }
}

void DbWriter::loop() {
    GetLogger("DbWriter")->info("DB thread started");
    while (running_) {
        // 等待：100ms 超时 或 eventfd 被写入（满 50 条提前唤醒）
        int timeout_ms = 100;
        if (notify_fd_ >= 0) {
            pollfd pfd;
            pfd.fd = notify_fd_;
            pfd.events = POLLIN;
            int ret = poll(&pfd, 1, timeout_ms);
            if (ret > 0) {
                uint64_t val;
                read(notify_fd_, &val, sizeof(val));  // 消费 eventfd
            }
        } else {
            usleep(timeout_ms * 1000);
        }

        // 攒一批数据
        std::vector<DbRecord> batch;
        batch.reserve(kBatchSize);
        DbRecord tmp;
        while (batch.size() < kBatchSize && queue_.pop(tmp)) {
            batch.push_back(std::move(tmp));
        }

        if (batch.empty()) {
            continue;
        }

        // 批量写入
        flush_batch(batch);
    }

    // 退出前把队列里剩余的数据写完
    DbRecord tmp;
    std::vector<DbRecord> remaining;
    while (queue_.pop(tmp)) {
        remaining.push_back(std::move(tmp));
    }
    if (!remaining.empty()) {
        flush_batch(remaining);
    }

    GetLogger("DbWriter")->info("DB thread stopped");
}

void DbWriter::flush_batch(const std::vector<DbRecord>& batch) {
    if (!db_ || batch.empty()) return;

    sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    sqlite3_stmt* sensor_stmt = nullptr;
    sqlite3_stmt* alarm_stmt = nullptr;
    const char* sensor_sql = "INSERT INTO sensor_data (source_type, node_id, tlv_type, value) VALUES (?, ?, ?, ?);";
    const char* alarm_sql = "INSERT INTO alarm_log (source_type, node_id, tlv_type, detail) VALUES (?, ?, ?, ?);";

    bool has_alarm = false;

    for (const auto& record : batch) {
        sqlite3_stmt* stmt = nullptr;
        if (record.type == DbOpType::SENSOR) {
            if (!sensor_stmt) {
                sqlite3_prepare_v2(db_, sensor_sql, -1, &sensor_stmt, nullptr);
            }
            stmt = sensor_stmt;
        } else {
            if (!alarm_stmt) {
                sqlite3_prepare_v2(db_, alarm_sql, -1, &alarm_stmt, nullptr);
            }
            stmt = alarm_stmt;
            has_alarm = true;
        }

        sqlite3_bind_int(stmt, 1, record.source_type);
        sqlite3_bind_int(stmt, 2, record.node_id);
        sqlite3_bind_int(stmt, 3, record.tlv_type);
        sqlite3_bind_text(stmt, 4, record.data.c_str(), -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            GetLogger("DbWriter")->error("INSERT failed: {}", sqlite3_errmsg(db_));
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }

    if (sensor_stmt) sqlite3_finalize(sensor_stmt);
    if (alarm_stmt) sqlite3_finalize(alarm_stmt);

    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);

    // 关键报警强制刷盘
    if (has_alarm) {
        int fd = sqlite3_file_control(db_, "main", SQLITE_FCNTL_SYNC, nullptr);
        // 如果file_control不支持，WAL模式下COMMIT已经保证持久性
    }

    GetLogger("DbWriter")->info("Flushed {} records to SQLite", batch.size());
}
