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
    pipeline.setCallback([&](const std::vector<gateway_engine::Detection>& dets) {
        logger->info("Detected {} objects", dets.size());
        for (const auto& d : dets) {
            InternalMessage msg;
            msg.source_type = 3;
            msg.tlv_type = 0x06;
            msg.payload.resize(sizeof(d));
            memcpy(msg.payload.data(), &d, sizeof(d));

            auto encoded = encode_internal_msg(msg);
            uds_client.write(encoded.data(), encoded.size());
        }
    });

    AnalysisScheduler scheduler(pipeline, uds_client.fd());
    scheduler.start();

    // 主循环心跳
    HeartbeatReporter hearbeat(pipeline, uds_client);
    while (g_running) {
        hearbeat.send_heartbeat();
        sleep(5);
    }

    scheduler.stop();
    pipeline.stop();

    logger->info("Shutting down...");
    return 0;
}