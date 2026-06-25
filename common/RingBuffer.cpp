#include "RingBuffer.h"
#include <cstddef>
#include <cstdlib>

// 构造函数初始化
RingBuffer::RingBuffer(size_t size){
    buffer_.resize(size);
    capacity_ = size;
    read_pos_ = 0;
    write_pos_ = 0;
}

// 环形数组，不需要析构
RingBuffer::~RingBuffer(){}

// 将新数据追加到后面，%容量实现环形
bool RingBuffer::append(const uint8_t* data, size_t len){
    if (len > available_to_write())
        return false; 
    for (int i = 0; i < len; i++){
        buffer_[write_pos_ % capacity_] = data[i];
        write_pos_++;
        write_pos_ %= capacity_;
    }
    return true;
}

// 直接从环形数组里面读取
bool RingBuffer::consume(size_t len){
    if (len > available_to_read()){
        return false;
    }
    read_pos_ += len;
    read_pos_ %= capacity_;
    return true;
}

// 获取读指针
uint8_t* RingBuffer::read_ptr(){
    return &buffer_[read_pos_];
}

// 获取剩余可读的数据长度
size_t RingBuffer::available_to_read() const{
    return (write_pos_ - read_pos_ + capacity_) % capacity_;
}

// 获取剩余可写的数据长度
size_t RingBuffer::available_to_write() const{
    return capacity_ - available_to_read() - 1;
}

// 重置环形数组
void RingBuffer::reset(){
    read_pos_ = 0;
    write_pos_ = 0;
}

