#pragma once

#include <vector>
#include <cstdint>
#include <atomic>
#include <thread>
#include <functional>
#include "../thirdparty/queue/queue/concurrentqueue.h"

namespace motioncam {
namespace mcraw_v3 {

class WriterQueue {
public:
    using DataBuffer = std::vector<uint8_t>;

    WriterQueue(std::function<void(const DataBuffer&)> writerFunc);
    ~WriterQueue();

    void enqueue(DataBuffer&& buffer);
    void stop();

private:
    void workerThread();

    moodycamel::ConcurrentQueue<DataBuffer> queue_;
    std::atomic<bool> running_;
    std::thread thread_;
    std::function<void(const DataBuffer&)> writerFunc_;
};

} // namespace mcraw_v3
} // namespace motioncam
