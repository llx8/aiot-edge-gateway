// pipeline/stages/InferenceStage.h
#pragma once
#include "StageBase.h"
#include "../PipelineQueue.h"
#include "../types.h"
#include <memory>

namespace gateway_engine {

struct InferenceResult {
    std::shared_ptr<Frame> frame;
    float* output = nullptr;     // 推理输出数据
    int output_size = 0;         // 输出总 float 数
};

class InferenceStage : public StageBase {
public:
    InferenceStage(
        PipelineQueue<std::shared_ptr<Frame>, 4>* input_queue,     
        PipelineQueue<InferenceResult, 4>* output_queue);          // 推理输出队列
        bool switch_model(const std::string& path);
protected:
    void run() override;
private:
    PipelineQueue<std::shared_ptr<Frame>, 4>* input_queue_;
    PipelineQueue<InferenceResult, 4>* output_queue_;
    std::string current_model_path_;
};

}