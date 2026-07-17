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
    PipelineQueue<std::shared_ptr<Frame>, 8>* input_queue,
    PipelineQueue<std::shared_ptr<Frame>, 8>* output_queue,
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

        // 创建黑布 + 贴图：输出 uint8 NHWC（RGB letterbox 图像，RKNN 内部归一化 /255）
        uint8_t* dst = frame->preprocessed_data;
        std::memset(dst, 0, input_size_ * input_size_ * 3);

        for (int r = 0; r < new_h; ++r) {
        for (int c = 0; c < new_w; ++c) {
            // GStreamer 产出 BGR → YOLO 训练用 RGB，写入时交换 R/B
            int si = (r * new_w + c) * 3;
            int di = ((r + pad_y) * input_size_ + (c + pad_x)) * 3;
            dst[di + 0] = resized[si + 2];  // R ← B
            dst[di + 1] = resized[si + 1];  // G ← G
            dst[di + 2] = resized[si + 0];  // B ← R
        }
    }

        frame->letterbox_scale = scale;
        frame->pad_x = pad_x;
        frame->pad_y = pad_y;

        output_queue_->push(frame);
    }
}
}
