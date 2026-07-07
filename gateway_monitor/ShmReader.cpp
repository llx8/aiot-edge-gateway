#include "ShmReader.h"
#include <sys/shm.h>
#include "Logger.h"

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

    // 读取当前索引
    uint32_t idx = ptr_->read_index.load(std::memory_order_acquire);
    // 更新last_read_index_
    last_read_index_ = idx;
    // 拷贝数据
    block = ptr_->buffers[idx];
    return true;
}

// 检查数据是否有更新
bool ShmReader::has_new_data() {
    if (!ptr_) return false;

    uint32_t idx = ptr_->read_index.load(std::memory_order_relaxed);
    // 直接检查两个缓冲区的索引, 只要有一个比last_read_index_大，就说明有新数据
    return idx != last_read_index_;
}