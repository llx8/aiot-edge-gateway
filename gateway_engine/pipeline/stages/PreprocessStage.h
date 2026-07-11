// PreprocessStage.h
#pragma once
#include "StageBase.h"
#include "../PipelineQueue.h"
#include "../types.h"
#include <memory>

namespace gateway_engine {

class PreprocessStage : public StageBase {
public:
    PreprocessStage(
        PipelineQueue<std::shared_ptr<Frame>, 4>* input_queue,    
        PipelineQueue<std::shared_ptr<Frame>, 4>* output_queue,   
        int input_size);
protected:
    void run() override;
private:
    PipelineQueue<std::shared_ptr<Frame>, 4>* input_queue_;   
    PipelineQueue<std::shared_ptr<Frame>, 4>* output_queue_;  
    int input_size_;
};
}