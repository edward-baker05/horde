#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace horde::core {

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    [[nodiscard]] size_t threadCount() const {
        return m_workers.size() + 1; // Workers + caller thread
    }

    template<typename Func>
    void parallelFor(size_t start, size_t end, Func&& func, size_t minChunkSize = 64) {
        if (start >= end) {
            return;
        }

        const size_t totalItems = end - start;
        const size_t totalThreads = threadCount();

        if (totalThreads <= 1 || totalItems <= minChunkSize) {
            func(start, end, 0);
            return;
        }

        // Calculate chunk size ensuring even distribution
        const size_t targetChunks = totalThreads * 4;
        const size_t calculatedChunk = std::max(minChunkSize, (totalItems + targetChunks - 1) / targetChunks);

        auto invoker = [](const void* callable, size_t chunkStart, size_t chunkEnd, size_t threadIdx) {
            const auto& fn = *reinterpret_cast<const std::decay_t<Func>*>(callable);
            fn(chunkStart, chunkEnd, threadIdx);
        };

        dispatchTask(invoker, &func, start, end, calculatedChunk);
    }

    static ThreadPool& defaultPool();

private:
    using TaskInvoker = void (*)(const void* callable, size_t start, size_t end, size_t threadIdx);

    void workerLoop(size_t threadIdx);
    void dispatchTask(TaskInvoker invoker, const void* callable, size_t start, size_t end, size_t chunkSize);

    std::vector<std::thread> m_workers;
    std::mutex m_mutex;
    std::condition_variable m_workCv;
    std::condition_variable m_doneCv;

    TaskInvoker m_currentInvoker = nullptr;
    const void* m_currentCallable = nullptr;
    std::atomic<size_t> m_nextIndex{0};
    size_t m_endIndex = 0;
    size_t m_chunkSize = 64;
    std::atomic<size_t> m_activeWorkers{0};
    uint64_t m_generation = 0;
    bool m_stopping = false;
};

} // namespace horde::core
