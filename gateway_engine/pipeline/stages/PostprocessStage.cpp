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

static float calc_iou(const Detection& a, const Detection& b) {
    float x1 = std::max(a.x, b.x);
    float y1 = std::max(a.y, b.y);
    float x2 = std::min(a.x + a.w, b.x + b.w);
    float y2 = std::min(a.y + a.h, b.y + b.h);
    
    float inter_w = std::max(0.0f, x2 - x1);
    float inter_h = std::max(0.0f, y2 - y1);
    float inter = inter_w * inter_h;
    
    float area_a = a.w * a.h;
    float area_b = b.w * b.h;
    float uni = area_a + area_b - inter;
    
    return (uni > 0.0f) ? inter / uni : 0.0f;
}

static inline float sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

void PostprocessStage::run() {
    while (running_.load()) {
        InferenceResult result;
        if (!input_queue_->pop(result)) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        std::vector<Detection> detections;

        if (result.output && result.output_size > 0 && result.frame) {
            static constexpr int kNumClasses = 80;
            static constexpr int kBoxChannels = 4;
            int stride = kBoxChannels + kNumClasses;
            int num_cells = result.output_size / stride;

            float scale = result.frame->letterbox_scale;
            int pad_x = result.frame->pad_x;
            int pad_y = result.frame->pad_y;

            for (int i = 0; i < num_cells; ++i) {
                float* cell = result.output + i * stride;

                float max_conf = 0.0f;
                int max_class = -1;
                for (int c = 0; c < kNumClasses; ++c) {
                    float score = sigmoid(cell[kBoxChannels + c]);
                    if (score > max_conf) {
                        max_conf = score;
                        max_class = c;
                    }
                }

                if (max_conf < conf_threshold_) continue;

                float cx = sigmoid(cell[0]);
                float cy = sigmoid(cell[1]);
                float w = std::exp(cell[2]);
                float h = std::exp(cell[3]);

                Detection det;
                det.class_id = max_class;
                det.confidence = max_conf;

                det.x = (cx - w * 0.5f - pad_x) / scale;
                det.y = (cy - h * 0.5f - pad_y) / scale;
                det.w = w / scale;
                det.h = h / scale;

                if (det.x < 0) det.x = 0;
                if (det.y < 0) det.y = 0;

                detections.push_back(det);
            }

            std::sort(detections.begin(), detections.end(),
                [](const Detection& a, const Detection& b) {
                    return a.confidence > b.confidence;
                });

            std::vector<Detection> nms_result;
            std::vector<bool> suppressed(detections.size(), false);

            for (size_t i = 0; i < detections.size(); ++i) {
                if (suppressed[i]) continue;
                nms_result.push_back(detections[i]);
                for (size_t j = i + 1; j < detections.size(); ++j) {
                    if (suppressed[j]) continue;
                    if (detections[i].class_id != detections[j].class_id) continue;
                    if (calc_iou(detections[i], detections[j]) > iou_threshold_) {
                        suppressed[j] = true;
                    }
                }
            }

            detections = std::move(nms_result);
        }

        free(result.output);
        result.output = nullptr;

        if (callback_) {
            callback_(detections);
        }
    }
}

}
