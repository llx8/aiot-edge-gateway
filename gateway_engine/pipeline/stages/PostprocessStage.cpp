#include "PostprocessStage.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstring>
#include <jpeglib.h>
#include <cstdio>

namespace gateway_engine {

PostprocessStage::PostprocessStage(
    PipelineQueue<InferenceResult, 4>* input_queue,
    float conf_threshold, float iou_threshold, int input_size, int jpeg_quality)
    : input_queue_(input_queue), conf_threshold_(conf_threshold),
      iou_threshold_(iou_threshold), input_size_(input_size), jpeg_quality_(jpeg_quality) {}

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

std::vector<uint8_t> PostprocessStage::encode_jpeg(const InferenceResult& result) {
    if (!result.frame || !result.frame->data) return {};

    auto& frame = *result.frame;
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    unsigned char* outbuf = nullptr;
    unsigned long outlen = 0;
    jpeg_mem_dest(&cinfo, &outbuf, &outlen);

    cinfo.image_width = frame.width;
    cinfo.image_height = frame.height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, jpeg_quality_, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    // BGR → RGB 逐行写入
    int row_stride = frame.width * 3;
    std::vector<uint8_t> rgb_row(row_stride);
    while (cinfo.next_scanline < cinfo.image_height) {
        uint8_t* src = frame.data + cinfo.next_scanline * row_stride;
        for (int c = 0; c < frame.width; c++) {
            rgb_row[c * 3 + 0] = src[c * 3 + 2];  // R ← B
            rgb_row[c * 3 + 1] = src[c * 3 + 1];  // G ← G
            rgb_row[c * 3 + 2] = src[c * 3 + 0];  // B ← R
        }
        uint8_t* row_ptr = rgb_row.data();
        jpeg_write_scanlines(&cinfo, &row_ptr, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    std::vector<uint8_t> jpeg_buf(outbuf, outbuf + outlen);
    free(outbuf);
    return jpeg_buf;
}

void PostprocessStage::run() {
    while (running_.load()) {
        InferenceResult result;
        if (!input_queue_->pop(result)) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        std::vector<Detection> detections;
        std::vector<uint8_t> jpeg_data;

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

            // 有检测结果时生成 JPEG 快照
            if (!detections.empty()) {
                jpeg_data = encode_jpeg(result);
            }
        }

        free(result.output);
        result.output = nullptr;

        if (callback_) {
            callback_(detections, jpeg_data);
        }
    }
}

}