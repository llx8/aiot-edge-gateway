// pipeline/stages/PostprocessStage.h
#pragma once
#include "StageBase.h"
#include "../PipelineQueue.h"
#include "../types.h"
#include "InferenceStage.h"
#include <vector>
#include <functional>

namespace gateway_engine {

class PostprocessStage : public StageBase {
public:
    PostprocessStage(
        PipelineQueue<InferenceResult, 4>* input_queue,
        float conf_threshold,
        float iou_threshold,
        int input_size,
        int jpeg_quality = 75);

    void setCallback(DetectionCallback cb);
    void setOnFrameDone(std::function<void()> cb) { on_frame_done_ = std::move(cb); }
protected:
    void run() override;
private:
    PipelineQueue<InferenceResult, 4>* input_queue_;
    float conf_threshold_;
    float iou_threshold_;
    int input_size_;
    int jpeg_quality_;
    DetectionCallback callback_;
    std::function<void()> on_frame_done_;

    // JPEG 编码：从 Frame 原始数据生成 JPEG
    std::vector<uint8_t> encode_jpeg(const InferenceResult& result);
};

}