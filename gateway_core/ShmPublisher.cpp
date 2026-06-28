#include "ShmPublisher.h"
#include "Logger.h"
#include <sys/shm.h>
#include <cstring>
#include <cerrno>
#include <stdexcept>

// 构造函数
ShmPublisher::ShmPublisher(key_t key) 
    : shmid_(-1)
    , ptr_(nullptr)
{
    // 创建共享内存
    GetLogger("gateway")->info("Creating shared memory with key: {}", key);
    shmid_ = shmget(key, sizeof(ShmBlock), IPC_CREAT | 0666);
    if (shmid_ < 0) {
        throw std::runtime_error("shmget failed: " + std::string(strerror(errno)));
    }

    // 映射共享内存
    ptr_ = static_cast<ShmBlock*>(shmat(shmid_, nullptr, 0));
    if (ptr_ == reinterpret_cast<ShmBlock*>(-1)) {
        throw std::runtime_error("shmat failed: " + std::string(strerror(errno)));
    }

    // 初始化共享内存
    // 设置魔数
    if (ptr_->magic != SHM_MAGIC) {
        std::memset(ptr_, 0, sizeof(ShmBlock));
        ptr_->magic = SHM_MAGIC;
    }
}

// 析构函数
ShmPublisher::~ShmPublisher() {
    if (ptr_ && ptr_ != reinterpret_cast<ShmBlock*>(-1)) {
        shmdt(ptr_);
    }
}

// 发布数据
void ShmPublisher::publish(const ShmBlock& block) {
    memcpy(ptr_, &block, sizeof(ShmBlock));
    ptr_->magic = SHM_MAGIC; // 确保魔数正确
}