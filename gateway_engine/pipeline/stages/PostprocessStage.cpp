#include "PostprocessStage.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>

namespace gateway_engine {

PostprocessStage::PostprocessStage(
    PipelineQueue<InferenceResult, 4>* input_queue,
    float conf_threshold, float iou_threshold, int input_size)
    : input_queue_(input_queue), conf_threshold_(conf_threshold),
      iou_threshold_(iou_threshold), input_size_(input_size) {}

void PostprocessStage::setCallback(DetectionCallback cb) {
    callback_ = std::move(cb);
}

// IoU 计算: 两个框的交集面积 / 并集面积
static float calc_iou(const Detection& a, const Detection& b) {
    float x1 = std::max(a.x, b.x);
    float y1 = std::max(a.y, b.y);
    float x2 = std::min(a.x + a.w, b.x + b.w);
    float y2 = std::min(a.y + a.h, b.y + b.h);
    
    float inter_w = std::max(0.0f, x2 - x1);
    float inter_h = std::max(0.0f, y2 - y1);
    float inter = inter_w * inter_h;  // 交集面积
    
    float area_a = a.w * a.h;
    float area_b = b.w * b.h;
    float uni = area_a + area_b - inter;
    
    return (uni > 0.0f) ? inter / uni : 0.0f;
}

void PostprocessStage::run() {
    while (running_.load()) {
        InferenceResult result;
        if (!input_queue_->pop(result)) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        std::vector<Detection> detections;
        
        // mock 阶段: output 全零，无检测框
        // 正式阶段: 解析 output → 筛选 conf > threshold → NMS → 坐标反算

        // 释放推理输出内存
        delete[] result.output;
        result.output = nullptr;

        if (callback_) {
            callback_(detections);
        }
    }
}

}