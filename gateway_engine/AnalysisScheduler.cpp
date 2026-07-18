#include "AnalysisScheduler.h"
#include "InternalMessage.h"
#include "Logger.h"
#include <unistd.h>
#include <sys/socket.h>
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
        // n==0 表示对端 clean shutdown；errno 表连接 reset。
        // 原实现 `if (n <= 0) continue;` 在对端关闭时会让 read 一直返回 0 → 100% CPU busy-spin。
        // 这里改为退出循环，让进程 main 后续感知到调度线程死亡并退出 → watchdog 重新拉起。
        if (n == 0) {
            if (running_) {
                GetLogger("AnalysisScheduler")->warn("UDS peer (core) closed, scheduler thread exiting");
            }
            break;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EPIPE || errno == ENOTCONN || errno == ECONNRESET) {
                if (running_) {
                    GetLogger("AnalysisScheduler")->warn("UDS connection reset: {}", strerror(errno));
                }
                break;
            }
            continue;  // 其他 errno：短暂退避后重试（例如 EAGAIN——但 fd 是阻塞的，极少出现）
        }

        // 2. 解码成 InternalMessage 格式
        auto result = decode_internal_msg(buf, n);

        if (!result.ok) continue;

        // 3. 判断是什么指令
        if (result.msg.tlv_type == CMD_START_ANALYSIS) {
            GetLogger("gateway_engine")->info("AnalysisScheduler: received START_ANALYSIS");
            // 解析 payload 中的 camera/model 参数
            std::string payload_str(result.msg.payload.begin(), result.msg.payload.end());
            std::string model_path;
            // 简单解析: "camera=zone_A model=yolov8n.rknn"
            auto model_pos = payload_str.find("model=");
            if (model_pos != std::string::npos) {
                model_path = payload_str.substr(model_pos + 6);
                auto space = model_path.find(' ');
                if (space != std::string::npos) model_path = model_path.substr(0, space);
            }
            pipeline_.start(model_path);
        } else if (result.msg.tlv_type == CMD_STOP_ANALYSIS) {
            pipeline_.stop();
        } else if (result.msg.tlv_type ==  CMD_SWITCH_MODEL) {
            // payload 格式: "model_path|sha256"
            // OTA 下发带 sha256（校验完整性）；RPC 手动切换无 sha256（跳过校验）
            std::string payload_str(result.msg.payload.begin(), result.msg.payload.end());
            std::string model_path = payload_str;
            std::string expected_sha256;
            auto sep = payload_str.find('|');
            if (sep != std::string::npos) {
                model_path = payload_str.substr(0, sep);
                expected_sha256 = payload_str.substr(sep + 1);
            }
            bool ok = pipeline_.switch_model(model_path, expected_sha256);

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
    shutdown(uds_fd_, SHUT_RD);
    if (thread_.joinable()) {
        thread_.join();
    }
}