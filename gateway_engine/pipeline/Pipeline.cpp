#include "Pipeline.h"

namespace gateway_engine {

Pipeline::Pipeline(const PipelineConfig& cfg)
    : cfg_(cfg)
    , queue_1_(OverflowPolicy::DropOldest)   // OverflowPolicy::DropOldest
    , queue_2_(OverflowPolicy::Block)   // OverflowPolicy::Block
    , queue_3_(OverflowPolicy::Block)   // OverflowPolicy::Block
{}

Pipeline::~Pipeline() {}

void Pipeline::start(std::string model_path) {
    stop();

    if (model_path.empty()) {
        model_path = cfg_.model_path;
    }
    model_path_ = std::move(model_path);
    pool_ = std::make_unique<FramePool>(
        cfg_.frame_pool_size, cfg_.input_size, cfg_.input_size, 3);

    capture_     = std::make_unique<CaptureStage>(cfg_.video_path, cfg_.input_size, pool_.get(), &queue_1_);
    preprocess_  = std::make_unique<PreprocessStage>(&queue_1_, &queue_2_, cfg_.input_size); 
    inference_   = std::make_unique<InferenceStage>(&queue_2_, &queue_3_, model_path_);
    postprocess_ = std::make_unique<PostprocessStage>(&queue_3_, cfg_.conf_threshold, cfg_.iou_threshold, cfg_.input_size, cfg_.jpeg_quality);

    // 倒序启动
    postprocess_->start();
    inference_->start();
    preprocess_->start();  
    capture_->start();  
}

void Pipeline::stop() {
    capture_->stop();       // 正序停止
    preprocess_->stop();
    inference_->stop();
    postprocess_->stop();
}

void Pipeline::setCallback(DetectionCallback cb) {
    if (postprocess_) postprocess_->setCallback(std::move(cb));
}

void Pipeline::on_frame_done() {
    frame_count_++;

    auto now = std::chrono::steady_clock::now();
    latency_samples_.push_back(now);
    if (latency_samples_.size() > kMaxLatencySamples) {
        latency_samples_.pop_front();
    }
}

// 计算帧率
void Pipeline::tick_fps() {
    current_fps_ = frame_count_.exchange(0);
}

float Pipeline::fps() const {
    return current_fps_.load();
}

bool Pipeline::switch_model(const std::string& path) {
    // 通知推理阶段切换模型
    return inference_->switch_model(path);
}

float Pipeline::avg_latency_ms() const {
    if (latency_samples_.size() < 2) return 0.0f;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(latency_samples_.back() - latency_samples_.front());
    return static_cast<float>(ms.count()) / (latency_samples_.size() - 1);
}

void Pipeline::set_throttle(bool enabled) {
    if (capture_) {
        capture_->set_throttle(enabled);
    }
}

}