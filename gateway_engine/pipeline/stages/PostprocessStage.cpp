#include "PostprocessStage.h"
#include "Logger.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstring>
#include <jpeglib.h>
#include <cstdio>

namespace gateway_engine {

PostprocessStage::PostprocessStage(
    PipelineQueue<InferenceResult, 8>* input_queue,
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
    // YOLOv5s COCO 锚框（3 尺度，每尺度 3 个 anchor，成对 w/h）
    struct AnchorSet { int stride; const int anchor[6]; };
    static const AnchorSet kAnchors[] = {
        {8,  {10, 13, 16, 30, 33, 23}},
        {16, {30, 61, 62, 45, 59, 119}},
        {32, {116, 90, 156, 198, 373, 326}},
    };
    static constexpr int kNumClasses = 80;
    static constexpr int kPropBoxSize = 4 + 1 + kNumClasses;  // 85：4 框 + 1 obj + 80 类

    while (running_.load()) {
        InferenceResult result;
        if (!input_queue_->pop(result)) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        using clock = std::chrono::steady_clock;
        static auto last_ts = clock::now();
        auto now = clock::now();
        auto gap_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_ts).count();
        last_ts = now;

        auto t0 = now;

        std::vector<Detection> detections;
        std::vector<uint8_t> jpeg_data;

        if (result.frame && !result.outputs.empty()) {
            float scale = result.frame->letterbox_scale;
            int pad_x = result.frame->pad_x;
            int pad_y = result.frame->pad_y;

            // 遍历每个输出头（YOLOv5s 3 个尺度：80×80 / 40×40 / 20×20）
            for (const auto& out : result.outputs) {
                if (out.dims.size() < 4 || out.data.empty()) continue;
                // dims=[1,255,H,W]，H/W 为网格尺寸
                int grid_h = static_cast<int>(out.dims[2]);
                int grid_w = static_cast<int>(out.dims[3]);
                int stride = input_size_ / grid_h;
                const int* anchor = nullptr;
                for (const auto& as : kAnchors) {
                    if (as.stride == stride) { anchor = as.anchor; break; }
                }
                if (!anchor) continue;

                int grid_len = grid_h * grid_w;
                const float* d = out.data.data();
                for (int a = 0; a < 3; ++a) {
                    for (int i = 0; i < grid_h; ++i) {
                        for (int j = 0; j < grid_w; ++j) {
                            int cell = i * grid_w + j;
                            // 通道布局：channel = a*85 + field，按 [anchor][field][grid] 排布
                            float obj = sigmoid(d[(a * kPropBoxSize + 4) * grid_len + cell]);
                            if (obj < conf_threshold_) continue;  // obj 过低直接跳过，省算 80 类

                            // YOLOv5 v6+ 框解码
                            float x = sigmoid(d[(a * kPropBoxSize + 0) * grid_len + cell]) * 2.0f - 0.5f;
                            float y = sigmoid(d[(a * kPropBoxSize + 1) * grid_len + cell]) * 2.0f - 0.5f;
                            float w = sigmoid(d[(a * kPropBoxSize + 2) * grid_len + cell]) * 2.0f;
                            float h = sigmoid(d[(a * kPropBoxSize + 3) * grid_len + cell]) * 2.0f;

                            float cx = (x + j) * stride;
                            float cy = (y + i) * stride;
                            float bw = w * w * anchor[a * 2];
                            float bh = h * h * anchor[a * 2 + 1];

                            // 最大类别分数
                            float max_score = 0.0f;
                            int max_class = 0;
                            for (int c = 0; c < kNumClasses; ++c) {
                                float s = sigmoid(d[(a * kPropBoxSize + 5 + c) * grid_len + cell]);
                                if (s > max_score) { max_score = s; max_class = c; }
                            }
                            float conf = obj * max_score;
                            if (conf < conf_threshold_) continue;

                            Detection det;
                            det.class_id = max_class;
                            det.confidence = conf;
                            // 模型坐标(0..input_size) -> 原图坐标（去 letterbox 的 pad/scale）
                            det.x = (cx - bw * 0.5f - pad_x) / scale;
                            det.y = (cy - bh * 0.5f - pad_y) / scale;
                            det.w = bw / scale;
                            det.h = bh / scale;
                            if (det.x < 0) det.x = 0;
                            if (det.y < 0) det.y = 0;
                            detections.push_back(det);
                        }
                    }
                }
            }

            // NMS（按类别）
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

            jpeg_data = encode_jpeg(result);
        }

        if (callback_) {
            auto proc_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            GetLogger("gateway_engine")->info("gap={}ms proc={}ms det={} jpeg={}KB",
                gap_ms, proc_ms, detections.size(), jpeg_data.size() / 1024);
            callback_(detections, jpeg_data);
        }

        if (on_frame_done_) {
            on_frame_done_();
        }
    }
}

}