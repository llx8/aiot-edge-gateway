#ifndef __RING_BUFFER_H__
#define __RING_BUFFER_H__ 

#include <cstdint>
#include <vector>

class RingBuffer {
public:
    RingBuffer(size_t size);            // 构造函数指定缓冲区大小
    ~RingBuffer();                      // 析构函数

    bool append(const uint8_t* data, size_t len);     // 写入数据
    bool consume(size_t len);                         // 读取数据

    uint8_t* read_ptr();                              // 获取读取指针
    size_t available_to_read() const;                 // 获取可读数据长度
    size_t available_to_write() const;                // 获取可写长度

    void reset();                                     // 重置
private:
    std::vector<uint8_t> buffer_;                     // 存储缓冲区
    size_t capacity_;                                 // 缓冲区容量
    size_t read_pos_;                                 // 读取位置
    size_t write_pos_;                                // 写入位置
};

#endif