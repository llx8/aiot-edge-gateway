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
    // 多输出模型（YOLOv5s 有 3 个尺度头 [1,255,80/40/20,80/40/20]）
    struct Output {
        std::vector<float> data;            // 输出 float 数据
        std::vector<uint32_t> dims;         // 维度（如 [1,255,80,80]）
    };
    std::vector<Output> outputs;
};

class InferenceStage : public StageBase {
public:
    InferenceStage(
        PipelineQueue<std::shared_ptr<Frame>, 8>* input_queue,
        PipelineQueue<InferenceResult, 8>* output_queue,
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

    PipelineQueue<std::shared_ptr<Frame>, 8>* input_queue_;
    PipelineQueue<InferenceResult, 8>* output_queue_;

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
