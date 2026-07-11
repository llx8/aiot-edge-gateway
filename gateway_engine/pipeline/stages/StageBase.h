#pragma once
#include <atomic>
#include <thread>

namespace gateway_engine {
class StageBase {
public:
    virtual ~StageBase() = default;

    void start() {
        running_.store(true);
        thread_ = std::thread(&StageBase::run, this);
    }

    void stop() {
        running_.store(false);
        if (thread_.joinable()) {
            thread_.join();
        }
    }
protected:
    virtual void run() = 0;
    std::atomic<bool> running_{false};
    std::thread thread_;
};
}
