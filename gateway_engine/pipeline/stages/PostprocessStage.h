// pipeline/stages/PostprocessStage.h
#pragma once
#include "StageBase.h"
#include "../PipelineQueue.h"
#include "../types.h"
#include "InferenceStage.h"
#include <vector>
#include <functional>

namespace gateway_engine {

using DetectionCallback = std::function<void(const std::vector<Detection>&)>;

class PostprocessStage : public StageBase {
public:
    PostprocessStage(
        PipelineQueue<InferenceResult, 4>* input_queue,   // 推理输出队列输入
        float conf_threshold,
        float iou_threshold,
        int input_size);
    
    void setCallback(DetectionCallback cb);
protected:
    void run() override;
private:
    PipelineQueue<InferenceResult, 4>* input_queue_;
    float conf_threshold_;
    float iou_threshold_;
    int input_size_;
    DetectionCallback callback_;
};

}