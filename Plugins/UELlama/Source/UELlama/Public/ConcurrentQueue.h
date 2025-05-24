#pragma once

#include <deque>
#include <mutex>
#include <functional>
#include <memory>

// Thread-safe queue implementation using std::deque
template<typename T>
class ConcurrentQueue {
public:
    void enqueue(T&& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.emplace_back(std::move(value));
    }

    bool processQ() {
        T value;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.empty()) {
                return false;
            }
            value = std::move(queue_.front());
            queue_.pop_front();
        }
        value();
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    std::deque<T> queue_;
    mutable std::mutex mutex_;
}; 