#include "ShmPublisher.h"
#include "Logger.h"
#include <atomic>
#include <sys/shm.h>
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <unistd.h>

// 构造函数
ShmPublisher::ShmPublisher(key_t key) 
    : shmid_(-1)
    , ptr_(nullptr)
    , notify_fd_(-1)
{
    // 创建共享内存
    GetLogger("gateway")->info("Creating shared memory with key: {}", key);
    shmid_ = shmget(key, sizeof(ShmRegion), IPC_CREAT | 0666);
    if (shmid_ < 0) {
        throw std::runtime_error("shmget failed: " + std::string(strerror(errno)));
    }

    // 映射共享内存
    ptr_ = static_cast<ShmRegion*>(shmat(shmid_, nullptr, 0));
    if (ptr_ == reinterpret_cast<ShmRegion*>(-1)) {
        throw std::runtime_error("shmat failed: " + std::string(strerror(errno)));
    }

    // 初始化共享内存
    std::memset(ptr_, 0, sizeof(ShmRegion));
}

// 析构函数
ShmPublisher::~ShmPublisher() {
    if (ptr_ && ptr_ != reinterpret_cast<ShmRegion*>(-1)) {
        shmdt(ptr_);
    }
}

// 发布数据
void ShmPublisher::publish(const ShmBlock& block) {
    // 直接读标记索引
    int idx = ptr_->read_index.load(std::memory_order_acquire);
    int indx = 1 - idx; // 选择另一个缓冲区

    // 复制数据
    ptr_->buffers[indx] = block;
    
    // 更新读索引
    ptr_->read_index.store(indx,std::memory_order_release);

    // 通知qt
    if (notify_fd_ >= 0) {
        uint64_t val = 1;
        ssize_t ret = write(notify_fd_, &val, sizeof(val));
        if (ret != sizeof(val)) {
            GetLogger("gateway")->warn("write eventfd failed: fd={} ret={} errno={}",
                notify_fd_, ret, strerror(errno));
        }
    }
}

// 保存外部传来的fd
void ShmPublisher::set_notify_fd(int fd){
    notify_fd_ = fd;
}