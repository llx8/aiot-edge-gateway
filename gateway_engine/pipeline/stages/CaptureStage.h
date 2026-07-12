#pragma once
#include "StageBase.h"
#include "../PipelineQueue.h"
#include "../FramePool.h"
#include "../types.h"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <memory>
#include <string>

namespace gateway_engine {
class CaptureStage : public StageBase {
public:
    CaptureStage(const std::string& video_path, FramePool* pool, PipelineQueue<std::shared_ptr<Frame>, 4>* output_queue);
    ~CaptureStage() override;
protected:
    void run() override;
private:
    std::string video_path_;
    FramePool* pool_;
    PipelineQueue<std::shared_ptr<Frame>, 4>* output_queue_;
    GstElement* pipeline_ = nullptr;
    GstAppSink* appsink_ = nullptr;

    void build_pipeline();  // 新建/重建 GStreamer pipeline
};
}
