#include "Logger.h"
#include "Config.h"
#include "UdsClient.h"
#include "InternalMessage.h"
#include "pipeline/Pipeline.h"
#include <csignal>
#include <cstring>
#include <atomic>
#include <unistd.h>
#include <fstream>
#include <sys/stat.h>
#include "AnalysisScheduler.h"
#include "HeartbeatReporter.h"

static std::atomic<sig_atomic_t> g_running{1};
void signal_handler(int) { g_running.store(0); }

int main() {
    auto logger = GetLogger("gateway_engine");
    logger->info("Starting gateway engine...");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);  // UDS/JPEG write 失败不能杀进程

    mkdir("/tmp/gateway_watchdog", 0755);
    // ready 信号延迟到 UDS 连接 + Pipeline 创建完成后才写（见下方），
    // 否则初始化失败退出时 watchdog 仍误判已就绪

    // 写 PID 文件，供 setup_cgroups.sh 自动附加
    std::ofstream pid_file("/tmp/gateway_engine.pid");
    pid_file << getpid();
    pid_file.close();

    // 自动加入 cgroup（CPU≤60%, 内存≤2GB），失败不阻塞
    {
        std::ofstream cprocs("/sys/fs/cgroup/user.slice/user-1000.slice/gateway_engine/cgroup.procs");
        if (cprocs.is_open()) {
            cprocs << getpid() << "\n";
        }
    }

    // 确保模型目录存在
    mkdir("models", 0755);

    auto config = load_config("conf/gateway.conf");
    std::string uds_path = config["uds"]["engine_path"];
    UdsClient uds_client(uds_path, 3);
    if (!uds_client.is_connected()) {
        logger->error("Failed to connect UDS: {}", uds_path);
        return 1;
    }

    gateway_engine::PipelineConfig pipe_cfg;
    pipe_cfg.video_path = "video/video.mp4";
    pipe_cfg.model_path = "models/yolov5s.rknn";
    pipe_cfg.conf_threshold = 0.1f;

    gateway_engine::Pipeline pipeline(pipe_cfg);
    pipeline.setFatalCallback([&](const std::string& reason) {
        logger->error("FATAL: {}", reason);
        InternalMessage fatal_msg{};
        fatal_msg.source_type = 3;
        fatal_msg.node_id = 0;
        fatal_msg.tlv_type = TLV_FATAL;
        fatal_msg.payload.assign(reason.begin(), reason.end());
        auto encoded = encode_internal_msg(fatal_msg);
        uds_client.write(encoded.data(), encoded.size());
    });

    pipeline.setCallback([&](const std::vector<gateway_engine::Detection>& dets,
                              const std::vector<uint8_t>& jpeg_data) {
        logger->info("Detected {} objects, jpeg={}KB", dets.size(), jpeg_data.size() / 1024);

        // 发送检测结果
        for (const auto& d : dets) {
            InternalMessage msg{};
            msg.source_type = 3;
            msg.node_id = 0;
            msg.tlv_type = 0x04;  // AI 视觉告警
            msg.payload.resize(sizeof(d));
            memcpy(msg.payload.data(), &d, sizeof(d));

            auto encoded = encode_internal_msg(msg);
            uds_client.write(encoded.data(), encoded.size());
        }

        // 发送 JPEG 快照（tlv_type=0x07），payload 格式：
        // [4B: num_detections] + [num_detections * 24B: Detection] + [JPEG data]
        if (!jpeg_data.empty()) {
            InternalMessage jpeg_msg{};
            jpeg_msg.source_type = 3;
            jpeg_msg.node_id = 0;
            jpeg_msg.tlv_type = 0x07;
            int32_t num = static_cast<int32_t>(dets.size());
            jpeg_msg.payload.resize(4 + num * sizeof(gateway_engine::Detection) + jpeg_data.size());
            std::memcpy(jpeg_msg.payload.data(), &num, 4);
            if (num > 0) {
                std::memcpy(jpeg_msg.payload.data() + 4, dets.data(),
                    num * sizeof(gateway_engine::Detection));
            }
            std::memcpy(jpeg_msg.payload.data() + 4 + num * sizeof(gateway_engine::Detection),
                jpeg_data.data(), jpeg_data.size());

            auto encoded = encode_internal_msg(jpeg_msg);
            ssize_t written = uds_client.write(encoded.data(), encoded.size());
            if (written != (ssize_t)encoded.size()) {
                logger->warn("JPEG snapshot send failed: {}/{}", written, encoded.size());
            }
        }
    });

    AnalysisScheduler scheduler(pipeline, uds_client.fd());
    scheduler.start();

    // 初始化全部完成，此时才通知 watchdog 就绪
    std::ofstream("/tmp/gateway_watchdog/gateway_engine.ready") << "1";

    // 主循环心跳
    HeartbeatReporter heartbeat(pipeline, uds_client);
    static constexpr float kNpuThrottleTempC = 85.0f;
    while (g_running.load()) {
        heartbeat.send_heartbeat();

        // NPU 过热保护：> 85°C 自动降帧率
        float npu_temp = heartbeat.last_npu_temp();
        if (npu_temp > kNpuThrottleTempC) {
            pipeline.set_throttle(true);
            logger->warn("NPU 过热保护已触发: temp={:.1f}°C, 降帧率", npu_temp);
        } else if (npu_temp > 0 && npu_temp < kNpuThrottleTempC - 5.0f) {
            // 温度回落 5°C 后才恢复，避免频繁切换
            pipeline.set_throttle(false);
        }

        sleep(5);
    }

    scheduler.stop();
    pipeline.stop();

    logger->info("Shutting down...");
    return 0;
}