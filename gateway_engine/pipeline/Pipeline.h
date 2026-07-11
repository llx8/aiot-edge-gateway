#pragma once
#include "types.h"
#include "PipelineQueue.h"
#include "FramePool.h"
#include "stages/CaptureStage.h"
#include "stages/PreprocessStage.h"
#include "stages/InferenceStage.h"
#include "stages/PostprocessStage.h"
#include <memory>

namespace gateway_engine {

class Pipeline {
public:
    explicit Pipeline(const PipelineConfig& cfg);
    ~Pipeline();
    void start();
    void stop();
    void setCallback(DetectionCallback cb);

private:
    PipelineConfig cfg_;
    std::unique_ptr<FramePool> pool_;
    
    // 三条队列
    PipelineQueue<std::shared_ptr<Frame>, 4> queue_1_;  
    PipelineQueue<std::shared_ptr<Frame>, 4> queue_2_;  
    PipelineQueue<InferenceResult, 4> queue_3_;         

    // 四个 Stage
    std::unique_ptr<CaptureStage> capture_;
    std::unique_ptr<PreprocessStage> preprocess_;
    std::unique_ptr<InferenceStage> inference_;
    std::unique_ptr<PostprocessStage> postprocess_;
};

}