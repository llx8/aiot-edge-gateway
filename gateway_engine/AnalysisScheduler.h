#pragma once
#include <thread>
#include "pipeline/Pipeline.h"
#include <atomic>
#include <vector>

class AnalysisScheduler {
public:
    explicit AnalysisScheduler(gateway_engine::Pipeline& pipeline, int uds_fd);
    ~AnalysisScheduler();
    void start(); // 启动线程
    void stop();  // 停止线程
private:
    void run(); // 线程主循环
    int uds_fd_; // Unix Domain Socket 文件描述符
    gateway_engine::Pipeline& pipeline_; // 引用 Pipeline 对象
    std::atomic<bool> running_; // 是否运行中
    std::thread thread_; // 线程对象
    std::vector<char> buffer_; // 缓冲区
};