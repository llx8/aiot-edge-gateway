#include "Logger.h"
#include "Config.h"
#include "UdsClient.h"
#include "InternalMessage.h"
#include "pipeline/Pipeline.h"
#include <csignal>
#include <cstring>
#include <unistd.h>
#include <fstream>
#include <sys/stat.h>
#include "AnalysisScheduler.h"
#include "HearbeatReporter.h"

static volatile sig_atomic_t g_running = 1;
void signal_handler(int) { g_running = 0; }

int main() {
    auto logger = GetLogger("gateway_engine");
    logger->info("Starting gateway engine...");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    mkdir("/tmp/gateway_watchdog", 0755);
    std::ofstream("/tmp/gateway_watchdog/gateway_engine.ready") << "1";

    auto config = load_config("conf/gateway.conf");
    std::string uds_path = config["uds"]["engine_path"];
    UdsClient uds_client(uds_path, 3);
    if (!uds_client.is_connected()) {
        logger->error("Failed to connect UDS: {}", uds_path);
        return 1;
    }

    gateway_engine::PipelineConfig pipe_cfg;
    pipe_cfg.video_path = "../video/video.mp4";
    pipe_cfg.model_path = "../model/rknn_model.rknn";

    gateway_engine::Pipeline pipeline(pipe_cfg);
    pipeline.setCallback([&](const std::vector<gateway_engine::Detection>& dets,
                              const std::vector<uint8_t>& jpeg_data) {
        logger->info("Detected {} objects, jpeg={}KB", dets.size(), jpeg_data.size() / 1024);

        // 发送检测结果
        for (const auto& d : dets) {
            InternalMessage msg;
            msg.source_type = 3;
            msg.tlv_type = 0x04;  // AI 视觉告警
            msg.payload.resize(sizeof(d));
            memcpy(msg.payload.data(), &d, sizeof(d));

            auto encoded = encode_internal_msg(msg);
            uds_client.write(encoded.data(), encoded.size());
        }

        // 发送 JPEG 快照（tlv_type=0x07）
        if (!jpeg_data.empty()) {
            InternalMessage jpeg_msg;
            jpeg_msg.source_type = 3;
            jpeg_msg.tlv_type = 0x07;
            jpeg_msg.payload = jpeg_data;

            auto encoded = encode_internal_msg(jpeg_msg);
            ssize_t written = uds_client.write(encoded.data(), encoded.size());
            if (written != (ssize_t)encoded.size()) {
                logger->warn("JPEG snapshot send failed: {}/{}", written, encoded.size());
            }
        }
    });

    AnalysisScheduler scheduler(pipeline, uds_client.fd());
    scheduler.start();

    // 主循环心跳
    HeartbeatReporter hearbeat(pipeline, uds_client);
    static constexpr float kNpuThrottleTempC = 85.0f;
    while (g_running) {
        hearbeat.send_heartbeat();

        // NPU 过热保护：> 85°C 自动降帧率
        float npu_temp = hearbeat.last_npu_temp();
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