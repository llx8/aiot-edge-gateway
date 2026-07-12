#include "FramePool.h"
#include <cstring>

namespace gateway_engine {
// 构造函数，初始化帧池大小
FramePool::FramePool(int pool_size, int width, int height, int channels)
    : pool_size_(pool_size)
    , free_head_(0)
    , free_list_(pool_size_)
    , width_(width)
    , height_(height)
    , channels_(channels)
{
    size_t buf_size = width_ * height_ * channels_;

    slots_.resize(pool_size_);
    frames_.resize(pool_size_);

    for (int i = 0; i < pool_size_; ++i) {
        slots_[i].data.resize(buf_size);
        slots_[i].preprocessed_data.resize(buf_size);

        frames_[i].data = slots_[i].data.data();
        frames_[i].preprocessed_data = slots_[i].preprocessed_data.data();
        frames_[i].width = width_;
        frames_[i].height = height_;
        frames_[i].channels = channels_;

        free_list_[i] = (i == pool_size_ - 1) ? -1 : i + 1;
    }
    free_head_.store(0, std::memory_order_release);
}

// 获取帧
std::shared_ptr<Frame> FramePool::get_frame() {
    int idx;
    do {
        idx = free_head_.load();
        if (idx == -1) return nullptr;
    }while (!free_head_.compare_exchange_strong(idx, free_list_[idx]));

    return std::shared_ptr<Frame>(&frames_[idx], [this, idx](Frame*) {
        release(idx);
    });
}

// 归还帧
void FramePool::release(int index) {
    free_list_[index] = free_head_.exchange(index);
}
}
