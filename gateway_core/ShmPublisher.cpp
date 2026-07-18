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
    if (ptr_ == (void*)-1) {
        throw std::runtime_error("shmat failed: " + std::string(strerror(errno)));
    }

    // 初始化共享内存
    std::memset(ptr_, 0, sizeof(ShmRegion));
    // seqlock 起始为 0（稳定）；ShmRegion 是 POD，memset 已置零，这里显式表达意图
    ptr_->seq[0].store(0, std::memory_order_relaxed);
    ptr_->seq[1].store(0, std::memory_order_relaxed);
    ptr_->read_index.store(0, std::memory_order_relaxed);
}

// 析构函数
ShmPublisher::~ShmPublisher() {
    if (ptr_ && ptr_ != (void*)-1) {
        shmdt(ptr_);
    }
    if (notify_fd_ >= 0) {
        close(notify_fd_);
        notify_fd_ = -1;
    }
}

// 发布数据
void ShmPublisher::publish(const ShmBlock& block) {
    // 直接读标记索引
    int idx = ptr_->read_index.load(std::memory_order_acquire);
    int indx = 1 - idx; // 选择另一个缓冲区

    // seqlock 写端：先把 seq 置奇（写入中），再拷贝数据，再置偶（写完）。
    // 读端若在写入过程中读到 seq 奇或 seq 变化，则重试。
    uint32_t cur = ptr_->seq[indx].load(std::memory_order_relaxed);
    ptr_->seq[indx].store(cur + 1, std::memory_order_release);  // 奇：写入中
    ptr_->buffers[indx] = block;
    ptr_->seq[indx].store(cur + 2, std::memory_order_release); // 偶：写入完成

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