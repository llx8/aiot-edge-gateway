#include "FramePool.h"
#include <cstring>

namespace gateway_engine {

FramePool::FramePool(int pool_size, int width, int height, int channels)
    : pool_size_(pool_size)
    , free_head_(0)
    , free_list_(pool_size_)
    , width_(width)
    , height_(height)
    , channels_(channels)
{
    size_t data_buf_size = width_ * height_ * channels_;                       // uint8 原始像素
    // preprocessed_data 也按 uint8 尺寸（PreprocessStage 输出 uint8 NHWC letterbox 图像，
    // RKNN 内部做归一化 /255，故预处理不需要 float 缓冲区）

    slots_.resize(pool_size_);
    frames_.resize(pool_size_);

    for (int i = 0; i < pool_size_; ++i) {
        slots_[i].data.resize(data_buf_size);
        slots_[i].preprocessed_data.resize(data_buf_size);

        frames_[i].data = slots_[i].data.data();
        frames_[i].preprocessed_data = slots_[i].preprocessed_data.data();
        frames_[i].width = width_;
        frames_[i].height = height_;
        frames_[i].channels = channels_;

        free_list_[i] = (i == pool_size_ - 1) ? -1 : i + 1;
    }
    free_head_.store(0, std::memory_order_release);
}

std::shared_ptr<Frame> FramePool::get_frame() {
    int idx;
    do {
        idx = free_head_.load(std::memory_order_acquire);
        if (idx == -1) return nullptr;
    } while (!free_head_.compare_exchange_weak(idx, free_list_[idx],
             std::memory_order_release, std::memory_order_relaxed));

    return std::shared_ptr<Frame>(&frames_[idx], [this, idx](Frame*) {
        release(idx);
    });
}

void FramePool::release(int index) {
    int old;
    do {
        old = free_head_.load(std::memory_order_acquire);
        free_list_[index] = old;
    } while (!free_head_.compare_exchange_weak(old, index,
             std::memory_order_release, std::memory_order_relaxed));
}

}
