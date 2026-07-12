#include "CaptureStage.h"
#include <cstring>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>

namespace gateway_engine {
CaptureStage::CaptureStage(const std::string& video_path, FramePool* pool, PipelineQueue<std::shared_ptr<Frame>, 4>* output_queue)
    : video_path_(video_path)
    , pool_(pool)
    , output_queue_(output_queue) {
        // 初始化GStreamer
        gst_init(nullptr, nullptr);
}

CaptureStage::~CaptureStage() {
    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
    }
}
void CaptureStage::run() {
    while (running_.load()) {
        if (pipeline_ == nullptr) {
            build_pipeline();
        }
        GstSample* sample = gst_app_sink_pull_sample(appsink_);
        if (!sample) {
            gst_element_set_state(pipeline_, GST_STATE_NULL);
            gst_object_unref(pipeline_);
            pipeline_ = nullptr;
            appsink_ = nullptr;
            std::this_thread::sleep_for(std::chrono::seconds(1)); // 等一秒再重试
            continue;
        }
        GstBuffer* buffer = gst_sample_get_buffer(sample);
        GstMapInfo map;
        gst_buffer_map(buffer, &map, GST_MAP_READ);

        auto frame = pool_->get_frame();
        if (frame) {
            // 将像素数据复制到帧中
            size_t copy_size = frame->width * frame->height * frame->channels;
            memcpy(frame->data, map.data, copy_size);
            output_queue_->push(frame);
        }
        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
    }
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
    }
}

void CaptureStage::build_pipeline() {
    // 这个函数独立建 pipeline
    std::string pipeline_str = "filesrc location=" + video_path_
        + " ! decodebin ! videoconvert ! appsink name=appsink";
    pipeline_ = gst_parse_launch(pipeline_str.c_str(), nullptr);

    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline_), "appsink");
    appsink_ = GST_APP_SINK(sink);

    gst_element_set_state(pipeline_, GST_STATE_PLAYING);
}

}
