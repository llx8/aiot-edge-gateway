#include "ShmReader.h"
#include <sys/shm.h>
#include "Logger.h"
#include <unistd.h>

ShmReader::ShmReader(key_t key)
    : shmid_(-1)
    , ptr_(nullptr)
    , last_read_index_(0)
{
    // shmget不加IPC_CREAT标志位，表示只获取已存在的共享内存
    shmid_ = shmget(key, sizeof(ShmRegion), 0666);
    if (shmid_ == -1) {
        // 打印日志
        GetLogger("gateway_monitor")->error("shmget failed: {}", strerror(errno));
        return;
    }
    // 将共享内存映射到当前进程的地址空间
    ptr_ = static_cast<ShmRegion*>(shmat(shmid_, nullptr, SHM_RDONLY));
    if (ptr_ == reinterpret_cast<ShmRegion*>(-1)) {
        GetLogger("gateway_monitor")->error("shmat failed: {}", strerror(errno));
        ptr_ = nullptr;
        return;
    }
    // 检查read_index是否合法
    uint32_t idx = ptr_->read_index.load(std::memory_order_acquire);
    if (idx > 1) {
        GetLogger("gateway_monitor")->error("Invalid read index: {}", idx);
        // 不置空ptr_, 后面read再检查read_index
    }
}

// 析构函数
ShmReader::~ShmReader() {
    if (ptr_ && ptr_ != reinterpret_cast<ShmRegion*>(-1)) {
        shmdt(ptr_);
    }
}

// 读取共享内存中的数据
bool ShmReader::read(ShmBlock& block) {
    if (!ptr_) return false;

    // seqlock 读端：重试若干次直到读到一致状态。
    // 写端在写入 buffers[idx] 前后翻转 seq 奇偶；若读端发现写入中（奇）或前后不一致，则重试。
    for (int retry = 0; retry < 4; ++retry) {
        uint32_t idx = ptr_->read_index.load(std::memory_order_acquire);
        if (idx > 1) return false;

        uint32_t s1 = ptr_->seq[idx].load(std::memory_order_acquire);
        if (s1 & 1u) {  // 写入中
            usleep(50);
            continue;
        }
        block = ptr_->buffers[idx];
        std::atomic_thread_fence(std::memory_order_acquire);
        uint32_t s2 = ptr_->seq[idx].load(std::memory_order_acquire);
        if (s1 == s2 && (s2 & 1u) == 0) {
            last_read_index_ = idx;
            return true;
        }
        // 不一致：被写端插入，重试
    }
    // 重试用尽（极少触发）：放弃本次读取，保留上一次数据
    return false;
}

// 检查数据是否有更新
bool ShmReader::has_new_data() {
    if (!ptr_) return false;

    uint32_t idx = ptr_->read_index.load(std::memory_order_relaxed);
    // 直接检查两个缓冲区的索引, 只要有一个比last_read_index_大，就说明有新数据
    return idx != last_read_index_;
}