#include "CaptureStage.h"
#include "Logger.h"
#include <cstring>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>

namespace gateway_engine {

gboolean on_bus_message(GstBus*, GstMessage* msg, gpointer data) {
    auto* self = static_cast<CaptureStage*>(data);
    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS) {
        self->eos_seen_.store(true);
        gst_element_seek(self->pipeline_, 1.0, GST_FORMAT_TIME,
                         static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SEGMENT),
                         GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
    }
    return TRUE;
}

CaptureStage::CaptureStage(const std::string& video_path, int input_size, FramePool* pool, PipelineQueue<std::shared_ptr<Frame>, 8>* output_queue)
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

std::string CaptureStage::make_pipeline_str() const {
    return "filesrc location=" + video_path_ +
           " ! qtdemux ! h264parse"
           " ! mppvideodec"
           " ! videoconvert"
           " ! video/x-raw,format=BGR,width=" + std::to_string(input_size_) +
           ",height=" + std::to_string(input_size_) +
           " ! appsink name=appsink";
}

void CaptureStage::destroy_pipeline() {
    if (pipeline_) {
        if (bus_watch_id_ > 0) {
            g_source_remove(bus_watch_id_);
            bus_watch_id_ = 0;
        }
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
    gst_app_sink_set_max_buffers(appsink_, 4);
    gst_app_sink_set_drop(appsink_, false);

    eos_seen_.store(false);
    GstBus* bus = gst_element_get_bus(pipeline_);
    bus_watch_id_ = gst_bus_add_watch(bus, on_bus_message, this);
    gst_object_unref(bus);

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
            if (eos_seen_.exchange(false)) {
                continue;
            }
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
        if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            GetLogger("CaptureStage")->warn("gst_buffer_map failed, skip frame");
            gst_sample_unref(sample);
            continue;
        }

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
