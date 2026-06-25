#include "RingBuffer.h"
#include <cstddef>
#include <cstdlib>

RingBuffer::RingBuffer(size_t size){
    buffer_.resize(size);
    capacity_ = size;
    read_pos_ = 0;
    write_pos_ = 0;
}

RingBuffer::~RingBuffer(){}

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

bool RingBuffer::consume(size_t len){
    if (len > available_to_read()){
        return false;
    }
    read_pos_ += len;
    read_pos_ %= capacity_;
    return true;
}

uint8_t* RingBuffer::read_ptr(){
    return &buffer_[read_pos_];
}

size_t RingBuffer::available_to_read() const{
    return (write_pos_ - read_pos_ + capacity_) % capacity_;
}

size_t RingBuffer::available_to_write() const{
    return capacity_ - available_to_read() - 1;
}

void RingBuffer::reset(){
    read_pos_ = 0;
    write_pos_ = 0;
}

