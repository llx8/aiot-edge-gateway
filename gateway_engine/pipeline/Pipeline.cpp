#include "Pipeline.h"

namespace gateway_engine {

Pipeline::Pipeline(const PipelineConfig& cfg)
    : cfg_(cfg)
    , queue_1_(OverflowPolicy::DropOldest)   // OverflowPolicy::DropOldest
    , queue_2_(OverflowPolicy::Block)   // OverflowPolicy::Block
    , queue_3_(OverflowPolicy::Block)   // OverflowPolicy::Block
{}

Pipeline::~Pipeline() {}

void Pipeline::start() {
    pool_ = std::make_unique<FramePool>(
        cfg_.frame_pool_size, cfg_.input_size, cfg_.input_size, 3);

    capture_     = std::make_unique<CaptureStage>(cfg_.video_path, pool_.get(), &queue_1_);
    preprocess_  = std::make_unique<PreprocessStage>(&queue_1_, &queue_2_, cfg_.input_size); 
    inference_   = std::make_unique<InferenceStage>(&queue_2_, &queue_3_);
    postprocess_ = std::make_unique<PostprocessStage>(&queue_3_, cfg_.conf_threshold, cfg_.iou_threshold, cfg_.input_size);

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

}