#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace gateway_engine {
// 帧数据
struct Frame {
    // 原始像素
    uint8_t* data = nullptr; // 原始像素数据指针
    int width = 0; // 原始宽度
    int height = 0; // 原始高度
    int channels = 3; // 原始通道数
    // 处理后的像素
    uint8_t* preprocessed_data = nullptr;
    // 元信息
    int64_t timestamp = 0; // 时间戳，单位：微秒
    int64_t frame_id = 0; // 帧ID，用于唯一标识每一帧视频
    // letterbox参数
    float letterbox_scale = 1.0f; // letterbox缩放比例
    int pad_x = 0; // letterbox填充宽度
    int pad_y = 0; // letterbox填充高度
};

// 一个检测框
struct Detection {
    float x, y, w, h; // 框坐标
    float confidence; // 框置信度
    int class_id; // 框分类ID
};

// pipline 配置
struct PipelineConfig {
    // 模型文件路径
    std::string model_path = "models/yolov5s.rknn";
    // 视频文件路径
    std::string video_path = "video/video.mp4";
    // 模型文件 SHA256（可选，用于校验模型完整性）
    std::string model_sha256;
    // 模型输入尺寸
    int input_size = 640;
    // 置信度阈值
    float conf_threshold = 0.5f;
    // IOU阈值 --- 用于非极大值抑制
    float iou_threshold = 0.5f;
    // SPSC无锁队列容量
    int queue_capacity = 4;
    // 帧内存池大小
    int frame_pool_size = 6;
    // JPEG 快照质量 (1-100)
    int jpeg_quality = 75;
};

// 检测回调：detections + 可选 JPEG 快照
using DetectionCallback = std::function<void(
    const std::vector<Detection>& detections,
    const std::vector<uint8_t>& jpeg_data)>;
}