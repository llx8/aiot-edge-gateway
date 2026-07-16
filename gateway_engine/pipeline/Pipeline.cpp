#include "Pipeline.h"
#include <cstdlib>

namespace gateway_engine {

Pipeline::Pipeline(const PipelineConfig& cfg)
    : cfg_(cfg)
    , queue_1_(OverflowPolicy::Block)   // Block：避免 SPSC 双消费者数据竞争（DropOldest 从生产者调 pop()）
    , queue_2_(OverflowPolicy::Block)   // OverflowPolicy::Block
    , queue_3_(OverflowPolicy::Block)   // OverflowPolicy::Block
{}

Pipeline::~Pipeline() {}

void Pipeline::start(std::string model_path) {
    stop();

    std::lock_guard<std::recursive_mutex> lock(stage_mutex_);

    if (model_path.empty()) {
        model_path = cfg_.model_path;
    }
    model_path_ = std::move(model_path);

    // 排空三条队列中上一轮残留的帧/结果（stop() 只停线程不排空队列）：
    // 1) queue_1_/queue_2_ 中的 shared_ptr<Frame> 指向旧 pool 的 frames_[]，
    //    若不排空，下面 pool_ 重建销毁旧池后这些指针即悬空，新 Stage pop 即 use-after-free。
    // 2) queue_3_ 的 InferenceResult.outputs 是 vector<float>，自动释放，pop 出来即可。
    {
        std::shared_ptr<Frame> f;
        while (queue_1_.pop(f)) {}
        while (queue_2_.pop(f)) {}
        InferenceResult r;
        while (queue_3_.pop(r)) {}
    }

    pool_ = std::make_unique<FramePool>(
        cfg_.frame_pool_size, cfg_.input_size, cfg_.input_size, 3);

    capture_     = std::make_unique<CaptureStage>(cfg_.video_path, cfg_.input_size, pool_.get(), &queue_1_);
    preprocess_  = std::make_unique<PreprocessStage>(&queue_1_, &queue_2_, cfg_.input_size);
    inference_   = std::make_unique<InferenceStage>(&queue_2_, &queue_3_, model_path_);
    postprocess_ = std::make_unique<PostprocessStage>(&queue_3_, cfg_.conf_threshold, cfg_.iou_threshold, cfg_.input_size, cfg_.jpeg_quality);
    postprocess_->setOnFrameDone([this]{ on_frame_done(); });

    // 倒序启动
    postprocess_->start();
    inference_->start();
    preprocess_->start();
    capture_->start();
}

void Pipeline::stop() {
    std::lock_guard<std::recursive_mutex> lock(stage_mutex_);
    if (capture_)     capture_->stop();      // 正序停止
    if (preprocess_)  preprocess_->stop();
    if (inference_)   inference_->stop();
    if (postprocess_) postprocess_->stop();

    // 排空所有队列中残留的帧/结果，释放 FramePool 引用
    {
        std::shared_ptr<Frame> f;
        while (queue_1_.pop(f)) {}
        while (queue_2_.pop(f)) {}
        InferenceResult r;
        while (queue_3_.pop(r)) {}
    }
}

void Pipeline::setCallback(DetectionCallback cb) {
    std::lock_guard<std::recursive_mutex> lock(stage_mutex_);
    if (postprocess_) postprocess_->setCallback(std::move(cb));
}

void Pipeline::setFatalCallback(std::function<void(const std::string&)> cb) {
    std::lock_guard<std::recursive_mutex> lock(stage_mutex_);
    if (inference_) inference_->set_fatal_callback(std::move(cb));
}

void Pipeline::on_frame_done() {
    frame_count_++;

    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(latency_mutex_);
        latency_samples_.push_back(now);
        if (latency_samples_.size() > kMaxLatencySamples) {
            latency_samples_.pop_front();
        }
    }
}

// 计算帧率
void Pipeline::tick_fps() {
    current_fps_ = frame_count_.exchange(0);
}

float Pipeline::fps() const {
    return current_fps_.load();
}

bool Pipeline::switch_model(const std::string& path, const std::string& expected_sha256) {
    std::lock_guard<std::recursive_mutex> lock(stage_mutex_);
    if (!inference_) return false;
    return inference_->switch_model(path, expected_sha256);
}

float Pipeline::avg_latency_ms() const {
    std::lock_guard<std::mutex> lock(latency_mutex_);
    if (latency_samples_.size() < 2) return 0.0f;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(latency_samples_.back() - latency_samples_.front());
    return static_cast<float>(ms.count()) / (latency_samples_.size() - 1);
}

void Pipeline::set_throttle(bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(stage_mutex_);
    if (capture_) {
        capture_->set_throttle(enabled);
    }
}

}