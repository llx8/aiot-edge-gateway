#pragma once

#include "SpscQueue.h"
#include <string>
#include <thread>
#include <atomic>
#include <cstdint>
#include <sqlite3.h>
#include <vector>

static constexpr int kDbQueueSize = 4096;
static constexpr int kBatchSize = 50;

enum class DbOpType { SENSOR, ALARM };

struct DbRecord {
    DbOpType type;
    int32_t source_type;
    int32_t node_id;
    uint8_t tlv_type;
    std::string data; // sensor data or alarm data
};

class DbWriter {
public:
    // 构造函数
    explicit DbWriter(const std::string& db_path);
    // 析构函数
    ~DbWriter();

    // 启动线程
    void start();
    // 停止线程
    void stop();

    // 主线程调用，塞数据进队列
    bool push(const DbRecord& record);

private:
    void loop();
    void flush_batch(const std::vector<DbRecord>& batch);

    SpscQueue<DbRecord, kDbQueueSize> queue_;
    std::thread db_thread_;
    std::atomic<bool> running_{false};

    sqlite3* db_{nullptr};
    void open_db(const std::string& db_path);
    // 创建数据库表
    void create_tables();  // 创建数据库表
};
