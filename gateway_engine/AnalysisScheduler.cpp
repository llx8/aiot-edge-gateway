#include "AnalysisScheduler.h"
#include "InternalMessage.h"
#include <unistd.h>
#include <cstdint>

// 构造函数
AnalysisScheduler::AnalysisScheduler(gateway_engine::Pipeline& pipeline, int uds_fd)
    : pipeline_(pipeline)
    , uds_fd_(uds_fd)
    , running_(false) {
    buffer_.resize(4096);
}

// 析构函数
AnalysisScheduler::~AnalysisScheduler() {
    stop();
}

void AnalysisScheduler::run() {
    while (running_) {
        // 1. 从 UDS fd 阻塞读取一条 InternalMessage
        uint8_t buf[4096];                    // 够装一条指令
        ssize_t n = read(uds_fd_, buf, sizeof(buf));
        if (n <= 0) continue;                 // 断开或错误，下次重试

        // 2. 解码成 InternalMessage 格式
        auto result = decode_internal_msg(buf, n);

        if (!result.ok) {
            // 解码失败，忽略
            continue;
        }

        // 3. 判断是什么指令
        if (result.msg.tlv_type == CMD_START_ANALYSIS) {
            pipeline_.start();
        } else if (result.msg.tlv_type == CMD_STOP_ANALYSIS) {
            pipeline_.stop();
        } else if (result.msg.tlv_type ==  CMD_SWITCH_MODEL) {
            std::string model_path(result.msg.payload.begin(), result.msg.payload.end());
            bool ok = pipeline_.switch_model(model_path);

            // 回复 ACK / NACK
            InternalMessage reply;
            reply.tlv_type = ok ? CMD_MODEL_ACK : CMD_MODEL_NACK;
            auto encoded = encode_internal_msg(reply);
            write(uds_fd_, encoded.data(), encoded.size());
        }
    }
}

void AnalysisScheduler::start() {
    running_ = true;
    thread_ = std::thread(&AnalysisScheduler::run, this);
}

void AnalysisScheduler::stop() {
    running_ = false;
    close(uds_fd_); // 关闭 UDS fd，唤醒阻塞的 read
    if (thread_.joinable()) {
        thread_.join();
    }
}