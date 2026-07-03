#pragma once

#include "ShmLayout.h"
#include <sys/types.h>

class ShmReader {
public:
    // 构造函数
    explicit ShmReader(key_t key);
    // 析构函数
    ~ShmReader();
    // 读取共享内存中的数据
    bool read(ShmBlock& block);
    // 检查数据是否有更新
    bool has_new_data();
private:
    int shmid_;
    ShmRegion* ptr_;
    uint64_t last_version_; // 上次读到的版本号
};