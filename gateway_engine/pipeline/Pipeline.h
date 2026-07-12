#pragma once
#include "types.h"
#include "PipelineQueue.h"
#include "FramePool.h"
#include "stages/CaptureStage.h"
#include "stages/PreprocessStage.h"
#include "stages/InferenceStage.h"
#include "stages/PostprocessStage.h"
#include <memory>
#include <atomic>

namespace gateway_engine {

class Pipeline {
public:
    explicit Pipeline(const PipelineConfig& cfg);
    ~Pipeline();
    void start();
    void stop();
    void setCallback(DetectionCallback cb);
    bool switch_model(const std::string& path);

    float fps() const;
    void on_frame_done(); // 每帧处理完成时调用
    void tick_fps(); // 计算帧率

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

    std::atomic<int> frame_count_{0}; // 统计帧数
    std::atomic<float> current_fps_{0.0f}; // 当前帧率
};

}