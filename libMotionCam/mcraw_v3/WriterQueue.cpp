#include "WriterQueue.hpp"

namespace motioncam {
namespace mcraw_v3 {

WriterQueue::WriterQueue(std::function<void(const DataBuffer&)> writerFunc)
    : writerFunc_(std::move(writerFunc)), running_(true) {
    thread_ = std::thread(&WriterQueue::workerThread, this);
}

WriterQueue::~WriterQueue() {
    stop();
}

void WriterQueue::enqueue(DataBuffer&& buffer) {
    if (running_) {
        queue_.enqueue(std::move(buffer));
    }
}

void WriterQueue::stop() {
    if (running_) {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }
}

void WriterQueue::workerThread() {
    DataBuffer buffer;
    while (running_) {
        if (queue_.try_dequeue(buffer)) {
            if (writerFunc_) {
                writerFunc_(buffer);
            }
        } else {
            // Give CPU a small break to prevent 100% usage if empty
            std::this_thread::yield();
        }
    }

    // Process remaining elements after stop is called
    while (queue_.try_dequeue(buffer)) {
        if (writerFunc_) {
            writerFunc_(buffer);
        }
    }
}

} // namespace mcraw_v3
} // namespace motioncam
