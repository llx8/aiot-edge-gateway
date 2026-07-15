// pipeline/stages/InferenceStage.h
#pragma once
#include "StageBase.h"
#include "../PipelineQueue.h"
#include "../types.h"
#include <memory>
#include <mutex>
#include <rknn/rknn_api.h>
#include <vector>
#include <string>

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
        PipelineQueue<InferenceResult, 4>* output_queue,
        const std::string& model_path);
    ~InferenceStage() override;

    // 热切换模型（可选传 SHA256 校验）
    bool switch_model(const std::string& model_path, const std::string& expected_sha256 = "");

    // 设置 FATAL 回调（旧模型回滚失败时触发）
    void set_fatal_callback(std::function<void(const std::string&)> cb) { fatal_cb_ = std::move(cb); }

protected:
    void run() override;

private:
    bool init_rknn(const std::string& model_path);
    void destroy_rknn();

    PipelineQueue<std::shared_ptr<Frame>, 4>* input_queue_;
    PipelineQueue<InferenceResult, 4>* output_queue_;

    // RKNN 相关
    rknn_context ctx_ = 0;
    rknn_input_output_num io_num_{};
    std::vector<rknn_tensor_attr> input_attrs_;
    std::vector<rknn_tensor_attr> output_attrs_;
    std::string model_path_;
    // 推理输入缓冲区（复用，避免反复分配）
    std::vector<uint8_t> input_buf_;
    int input_size_ = 0;          // 网络输入尺寸（如 640）

    mutable std::mutex model_mutex_;  // 保护 ctx_ 等热切换相关成员
    std::function<void(const std::string&)> fatal_cb_;  // FATAL 回调
};

} // namespace gateway_engine
