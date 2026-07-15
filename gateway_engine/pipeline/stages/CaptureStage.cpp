#include "CaptureStage.h"
#include <cstring>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>

namespace gateway_engine {

CaptureStage::CaptureStage(const std::string& video_path, int input_size, FramePool* pool, PipelineQueue<std::shared_ptr<Frame>, 4>* output_queue)
    : video_path_(video_path)
    , input_size_(input_size)
    , pool_(pool)
    , output_queue_(output_queue) {
    gst_init(nullptr, nullptr);
}

CaptureStage::~CaptureStage() {
    stop();
    destroy_pipeline();
}

bool CaptureStage::is_rtsp() const {
    return video_path_.find("rtsp://") == 0;
}

std::string CaptureStage::make_pipeline_str() const {
    if (is_rtsp()) {
        return "rtspsrc location=" + video_path_ +
               " latency=0 protocols=tcp"
               " ! rtph264depay ! h264parse"
               " ! mppvideodec"
               " ! videoconvert"
               " ! video/x-raw,format=BGR,width=" + std::to_string(input_size_) +
               ",height=" + std::to_string(input_size_) +
               " ! appsink name=appsink";
    } else {
        return "filesrc location=" + video_path_ +
               " ! qtdemux ! h264parse"
               " ! mppvideodec"
               " ! videoconvert"
               " ! video/x-raw,format=BGR,width=" + std::to_string(input_size_) +
               ",height=" + std::to_string(input_size_) +
               " ! appsink name=appsink";
    }
}

void CaptureStage::destroy_pipeline() {
    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        appsink_ = nullptr;
    }
}

bool CaptureStage::build_pipeline() {
    destroy_pipeline();

    std::string pipeline_str = make_pipeline_str();
    GError* error = nullptr;
    pipeline_ = gst_parse_launch(pipeline_str.c_str(), &error);
    if (error) {
        g_error_free(error);
        return false;
    }

    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline_), "appsink");
    if (!sink) {
        destroy_pipeline();
        return false;
    }
    appsink_ = GST_APP_SINK(sink);
    gst_app_sink_set_max_buffers(appsink_, 2);
    gst_app_sink_set_drop(appsink_, true);

    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        destroy_pipeline();
        return false;
    }
    return true;
}

void CaptureStage::run() {
    int reconnect_delay_ms = 1000;
    static constexpr int kMaxReconnectDelayMs = 30000;

    while (running_.load()) {
        if (pipeline_ == nullptr) {
            if (!build_pipeline()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_delay_ms));
                reconnect_delay_ms = std::min(reconnect_delay_ms * 2, kMaxReconnectDelayMs);
                continue;
            }
            reconnect_delay_ms = 1000;
        }

        GstSample* sample = gst_app_sink_pull_sample(appsink_);
        if (!sample) {
            destroy_pipeline();
            std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_delay_ms));
            reconnect_delay_ms = std::min(reconnect_delay_ms * 2, kMaxReconnectDelayMs);
            continue;
        }

        GstBuffer* buffer = gst_sample_get_buffer(sample);
        if (!buffer) {
            gst_sample_unref(sample);
            continue;
        }

        GstMapInfo map;
        gst_buffer_map(buffer, &map, GST_MAP_READ);

        auto frame = pool_->get_frame();
        if (frame) {
            size_t copy_size = frame->width * frame->height * frame->channels;
            memcpy(frame->data, map.data, std::min(copy_size, map.size));
            output_queue_->push(frame);
        }

        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);

        // NPU 过热保护：温度 > 85°C 时降帧率（约 5fps）
        if (throttle_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    destroy_pipeline();
}

}
