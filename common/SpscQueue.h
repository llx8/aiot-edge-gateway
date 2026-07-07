#pragma once

#include <atomic>
#include <array>
#include <cstddef>

static constexpr size_t kCacheLineSize = 64;

template <typename T, size_t Capacity>
class SpscQueue {
public:
    SpscQueue(){
        read_pos_.store(0); // 初始化读指针
        // 初始化写指针
        write_pos_.store(0);
    } // 构造函数

    bool push(const T& item){
        // 入队
        size_t pos = write_pos_.load(std::memory_order_relaxed);
        size_t next_pos = (pos + 1) % Capacity; // 计算下一个写指针位置
        // 检查队列是否已满
        if (next_pos == read_pos_.load(std::memory_order_acquire)) {
            return false; // 队列已满
        }
        buffer_[pos] = item; // 入队
        write_pos_.store(next_pos, std::memory_order_release); // 更新写指针
        return true; // 入队成功
    } // 生产者入队

    bool pop(T& item){
        // 出队
        size_t pos = read_pos_.load(std::memory_order_relaxed);
        // 检查队列是否为空
        if (pos == write_pos_.load(std::memory_order_acquire)) {
            return false; // 队列为空
        }
        item = buffer_[pos]; // 出队
        read_pos_.store((pos + 1) % Capacity, std::memory_order_release); // 更新读指针
        return true; // 出队成功
    } // 消费者出队
    bool empty() const {
        return size() == 0; // 检查队列是否为空
    } // 检查队列是否为空
    size_t size() const {
        return (write_pos_.load(std::memory_order_relaxed) - read_pos_.load(std::memory_order_relaxed) + Capacity) % Capacity; // 获取队列当前元素数量
    } // 获取队列当前元素数量
private:
    std::array<T, Capacity> buffer_; // 队列缓冲区
    alignas(kCacheLineSize) std::atomic<size_t> read_pos_; // 读指针
    alignas(kCacheLineSize) std::atomic<size_t> write_pos_; // 写指针
};
