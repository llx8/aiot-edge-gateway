#pragma once
#include "types.h"
#include <vector>
#include <memory>
#include <atomic>

namespace gateway_engine {
class FramePool{
public:
    // 构造函数
    FramePool(int pool_size, int width, int height, int channels);
    // 禁止拷贝、移动
    FramePool(const FramePool&) = delete;
    FramePool& operator=(const FramePool&) = delete;

    // 返回shared_ptr<Frame>, 用完自动归还
    std::shared_ptr<Frame> get_frame();
private:
    void release(int index); // 归还指定槽位到空闲链表
    struct Slot {
        // 原始buffer
        std::vector<uint8_t> data;
        // 预处理buffer
        std::vector<uint8_t> preprocessed_data;
    };
    int width_; // 帧宽度
    int height_; // 帧高度
    int channels_; // 帧通道数
    int pool_size_; // 帧池大小
    std::vector<Slot> slots_; // 槽位数组
    std::vector<Frame> frames_; // 帧数组
    // 空闲链表
    std::vector<int> free_list_;
    std::atomic<int> free_head_{-1}; // 空闲链表头指针
};
}