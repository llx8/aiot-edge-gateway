// PreprocessStage.cpp
#include "PreprocessStage.h"
#include <vector>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <thread>

namespace gateway_engine {
namespace {
void resize_bilinear(const uint8_t* src, int src_w, int src_h, uint8_t* dst, int dst_w, int dst_h) {
    // 计算缩放比例
    float scale_x = (float)src_w / dst_w;
    float scale_y = (float)src_h / dst_h;  

    for (int dy = 0; dy < dst_h; ++dy) {
        float sy = dy * scale_y;
        int y0 = (int)sy;
        int y1 = std::min(y0 + 1, src_h - 1);
        float fy = sy - y0;
        
        for (int dx = 0; dx < dst_w; ++dx) {
            float sx = dx * scale_x;  
            int x0 = (int)sx;
            int x1 = std::min(x0 + 1, src_w - 1);
            float fx = sx - x0;
            
            for (int c = 0; c < 3; ++c) {
                float v00 = src[(y0 * src_w + x0) * 3 + c];  
                float v01 = src[(y0 * src_w + x1) * 3 + c];  
                float v10 = src[(y1 * src_w + x0) * 3 + c];  
                float v11 = src[(y1 * src_w + x1) * 3 + c];  

                float top = v00 * (1 - fx) + v01 * fx;
                float bot = v10 * (1 - fx) + v11 * fx;
                dst[(dy * dst_w + dx) * 3 + c] = (uint8_t)(top * (1 - fy) + bot * fy);
            }
        }
    }
}
}  // namespace

PreprocessStage::PreprocessStage(
    PipelineQueue<std::shared_ptr<Frame>, 4>* input_queue,
    PipelineQueue<std::shared_ptr<Frame>, 4>* output_queue,
    int input_size)
    : input_queue_(input_queue), output_queue_(output_queue),
      input_size_(input_size) {}

void PreprocessStage::run() {
    while (running_.load()) {
        std::shared_ptr<Frame> frame;
        if (!input_queue_->pop(frame)) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        float scale = std::min((float)input_size_ / frame->width,(float)input_size_ / frame->height);
        int new_w = (int)(frame->width * scale);
        int new_h = (int)(frame->height * scale);
        int pad_x = (input_size_ - new_w) / 2;
        int pad_y = (input_size_ - new_h) / 2;

        // resize 到 (new_w, new_h)
        std::vector<uint8_t> resized(new_w * new_h * 3);
        resize_bilinear(frame->data, frame->width, frame->height, resized.data(), new_w, new_h);

        // 创建黑布，贴图
        float* pre = (float*)frame->preprocessed_data;
        memset(pre, 0, input_size_ * input_size_ * 3 * sizeof(float));

        for (int r = 0; r < new_h; ++r) {
        for (int c = 0; c < new_w; ++c) {
            for (int ch = 0; ch < 3; ++ch) {
                // 源位置 (HWC)
                int src_idx = (r * new_w + c) * 3 + ch;
                float val = resized[src_idx] / 255.0f;

                // 目标位置 (CHW，带 pad)
                int dst_r = r + pad_y;
                int dst_c = c + pad_x;
                int dst_idx = ch * input_size_ * input_size_
                            + dst_r * input_size_
                            + dst_c;
                pre[dst_idx] = val;
            }
        }
    }

        frame->letterbox_scale = scale;
        frame->pad_x = pad_x;
        frame->pad_y = pad_y;

        output_queue_->push(frame);
    }
}
}
