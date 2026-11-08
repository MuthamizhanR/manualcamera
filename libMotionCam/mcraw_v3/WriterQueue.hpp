#pragma once

#include <vector>
#include <cstdint>
#include <atomic>
#include <thread>
#include <functional>

#if __has_include("concurrentqueue.h")
#include "concurrentqueue.h"
#elif __has_include("../thirdparty/queue/queue/concurrentqueue.h")
#include "../thirdparty/queue/queue/concurrentqueue.h"
#else
#include "../../libMotionCam/thirdparty/queue/queue/concurrentqueue.h"
#endif

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

using WriterQueue = mcraw_v3::WriterQueue;

} // namespace motioncam
