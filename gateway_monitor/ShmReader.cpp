#include "ShmReader.h"
#include <sys/shm.h>
#include "Logger.h"

ShmReader::ShmReader(key_t key)
    : shmid_(-1)
    , ptr_(nullptr)
    , last_version_(0)
{
    // shmget不加IPC_CREAT标志位，表示只获取已存在的共享内存
    shmid_ = shmget(key, sizeof(ShmBlock), 0666);
    if (shmid_ == -1) {
        // 打印日志
        GetLogger("gateway_monitor")->error("shmget failed: {}", strerror(errno));
        return;
    }
    // 将共享内存映射到当前进程的地址空间
    ptr_ = static_cast<ShmBlock*>(shmat(shmid_, nullptr, SHM_RDONLY));
    if (ptr_ == reinterpret_cast<ShmBlock*>(-1)) {
        GetLogger("gateway_monitor")->error("shmat failed: {}", strerror(errno));
        ptr_ = nullptr;
        return;
    }
    // 检查magic, 确认B进程已经初始化共享内存
    if (ptr_->magic != SHM_MAGIC) {
        GetLogger("gateway_monitor")->error("Shared memory magic mismatch");
        // 不置空ptr_, 后面read再检查magic
    }
}

// 析构函数
ShmReader::~ShmReader() {
    if (ptr_ && ptr_ != reinterpret_cast<ShmBlock*>(-1)) {
        shmdt(ptr_);
    }
}

// 读取共享内存中的数据
bool ShmReader::read(ShmBlock& block) {
    if (!ptr_) return false;

    // 检查magic
    if (ptr_->magic != SHM_MAGIC) {
        GetLogger("gateway_monitor")->error("Shared memory magic mismatch");
        return false;
    }

    // 拷贝数据
    memcpy(&block, ptr_, sizeof(ShmBlock));
    last_version_ = block.version;
    return true;
}

// 检查数据是否有更新
bool ShmReader::has_new_data() {
    if (!ptr_) return false;

    // 检查magic
    if (ptr_->magic != SHM_MAGIC) {
        GetLogger("gateway_monitor")->error("Shared memory magic mismatch");
        return false;
    }

    return ptr_->version != last_version_;
}