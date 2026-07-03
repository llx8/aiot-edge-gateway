#ifndef SHM_BLOCK_H
#define SHM_BLOCK_H

#include <cstdint>

#define SHM_MAGIC  0x47574D4D

struct ShmBlock{
    uint32_t magic;         // 0x47574D4D
    uint64_t version;       // 版本
    int64_t uptime_sec;     // 运行时间
    int32_t total_packets;  // 总包数
    int32_t total_alarms;   // 总告警数
    float cpu_usage;        // cpu使用率
    float mem_usage;        // 内存使用率
    int32_t online_nodes;   // 在线节点数
    int32_t mqtt_connected; // mqtt连接数
    int32_t alarm_active;   // 激活的告警数
    char last_alarm[128];   // 最后一个告警
    // M2预留：Ping-Pong双缓冲扩展区
    uint8_t reserved[64];
};

#endif

