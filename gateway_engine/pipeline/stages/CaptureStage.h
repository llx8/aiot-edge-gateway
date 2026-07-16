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
    CaptureStage(const std::string& video_path, int input_size, FramePool* pool, PipelineQueue<std::shared_ptr<Frame>, 4>* output_queue);
    ~CaptureStage() override;
// NPU 过热保护：降帧率
    void set_throttle(bool enabled) { throttle_.store(enabled); }

protected:
    void run() override;
private:
    std::string video_path_;
    int input_size_;
    FramePool* pool_;
    PipelineQueue<std::shared_ptr<Frame>, 4>* output_queue_;
    GstElement* pipeline_ = nullptr;
    GstAppSink* appsink_ = nullptr;
    std::atomic<bool> throttle_{false};

    std::string make_pipeline_str() const;
    void destroy_pipeline();
    bool build_pipeline();
};
}
