#pragma once
#include "SpscQueue.h"
#include <thread>
#include <chrono>

namespace  gateway_engine {
enum class OverflowPolicy {
    Block, 
    DropOldest
};
template <typename T, size_t Capacity>
class PipelineQueue {
public:
    explicit PipelineQueue(OverflowPolicy policy)
        : policy_(policy) {}
    
    void push(const T& item) {
        if (policy_ == OverflowPolicy::DropOldest) {
            T dummy;
            while(!q_.push(item)){
                q_.pop(dummy);
            }
        }
        else{
            while(!q_.push(item)){
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    }
    bool pop(T& item) {
        return q_.pop(item);
    }
private:
    OverflowPolicy policy_;
    SpscQueue<T, Capacity> q_;
};
}