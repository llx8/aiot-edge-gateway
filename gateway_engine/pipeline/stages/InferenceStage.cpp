#include "InferenceStage.h"
#include <chrono>
#include <thread>
#include <unistd.h>

namespace gateway_engine {

InferenceStage::InferenceStage(
    PipelineQueue<std::shared_ptr<Frame>, 4>* input_queue,
    PipelineQueue<InferenceResult, 4>* output_queue)
    : input_queue_(input_queue), output_queue_(output_queue) {}

void InferenceStage::run() {
    while (running_.load()) {
        std::shared_ptr<Frame> frame;
        if (!input_queue_->pop(frame)) {   
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        InferenceResult result;
        result.frame = frame;

        // mock: 生成假输出 (模拟 YOLO 的 84×8400 输出)
        result.output_size = 84 * 8400;
        result.output = new float[result.output_size]();
        // ^ 全零输出 = 没有任何检测框

        output_queue_->push(result);   // 推理输出队列入队

        result.output = nullptr;
    }
}

bool InferenceStage::switch_model(const std::string& path) {
    if (access(path.c_str(), F_OK) != 0) {
        return false;
    }
    current_model_path_ = path;
    return true;
}
}