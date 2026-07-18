#ifndef SHM_BLOCK_H
#define SHM_BLOCK_H

#include <cstdint>
#include <atomic>

#define SHM_MAGIC  0x47574D4D

struct ShmBlock{
    int64_t uptime_sec;     // 运行时间
    int32_t total_packets;  // 总包数
    int32_t total_alarms;   // 总告警数
    float cpu_usage;        // cpu使用率
    float mem_usage;        // 内存使用率
    int32_t online_nodes;   // 在线节点数
    int32_t mqtt_connected; // mqtt连接数
    int32_t alarm_active;   // 激活的告警数
    char last_alarm[128];   // 最后一个告警
    float npu_temp_c;       // NPU温度
    float inference_fps;    // 推理帧率
    int32_t ai_engine_online; // 进程E是否在线
    int32_t model_version;    // 模型版本号
    uint32_t last_detection_ts; // 最后一次检测时间戳
    char last_model_name[64];   // 当前模型名称
    int32_t snapshot_jpeg_len;  // 最后一次快照JPEG长度
    float sensor_temp;         // 最新传感器温度
    float sensor_hum;          // 最新传感器湿度

    uint8_t _reserved[120]; // 预留空间，对齐设计文档
};

static_assert(sizeof(ShmBlock) <= 4096, "ShmBlock 超出页大小");

struct ShmRegion{
    ShmBlock buffers[2]; // 双缓冲区
    std::atomic<uint32_t> read_index; // 0或1， 标记当前可读的缓冲区索引
    std::atomic<uint32_t> seq[2];    // seqlock：偶数=稳定，奇数=写入中。读端需校验前后奇偶一致才可信
};

#endif

