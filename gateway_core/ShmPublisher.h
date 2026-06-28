#pragma once

#include "ShmLayout.h"
#include <sys/types.h>

class ShmPublisher {
public:
    // 构造函数
    explicit ShmPublisher(key_t key);

    // 析构函数
    ~ShmPublisher();

    // 发布数据
    void publish(const ShmBlock& block);
private:
    int shmid_;
    ShmBlock* ptr_;
};