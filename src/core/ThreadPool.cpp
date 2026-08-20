#include "core/ThreadPool.hpp"

namespace horde::core {

ThreadPool::ThreadPool(size_t numThreads) {
    if (numThreads == 0) {
        const unsigned int hw = std::thread::hardware_concurrency();
        numThreads = (hw > 0) ? static_cast<size_t>(hw) : 4;
    }

    // Number of background worker threads is total threads minus caller
    const size_t workerCount = (numThreads > 1) ? (numThreads - 1) : 0;
    m_workers.reserve(workerCount);

    for (size_t i = 0; i < workerCount; ++i) {
        m_workers.emplace_back(&ThreadPool::workerLoop, this, i + 1);
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
        m_generation++;
    }
    m_workCv.notify_all();

    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::workerLoop(size_t threadIdx) {
    uint64_t lastGen = 0;

    while (true) {
        TaskInvoker invoker = nullptr;
        const void* callable = nullptr;
        size_t chunkSize = 64;
        size_t endIdx = 0;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_workCv.wait(lock, [&]() {
                return m_stopping || m_generation > lastGen;
            });

            if (m_stopping) {
                return;
            }

            lastGen = m_generation;
            invoker = m_currentInvoker;
            callable = m_currentCallable;
            chunkSize = m_chunkSize;
            endIdx = m_endIndex;
        }

        if (invoker && callable) {
            while (true) {
                const size_t chunkStart = m_nextIndex.fetch_add(chunkSize, std::memory_order_relaxed);
                if (chunkStart >= endIdx) {
                    break;
                }
                const size_t chunkEnd = std::min(chunkStart + chunkSize, endIdx);
                invoker(callable, chunkStart, chunkEnd, threadIdx);
            }
        }

        // Notify that this worker is done with the current batch
        if (m_activeWorkers.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_doneCv.notify_one();
        }
    }
}

void ThreadPool::dispatchTask(
    TaskInvoker invoker, const void* callable, size_t start, size_t end, size_t chunkSize) {
    const size_t workerCount = m_workers.size();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_currentInvoker = invoker;
        m_currentCallable = callable;
        m_nextIndex.store(start, std::memory_order_relaxed);
        m_endIndex = end;
        m_chunkSize = chunkSize;
        m_activeWorkers.store(workerCount + 1, std::memory_order_release);
        m_generation++;
    }

    m_workCv.notify_all();

    // The caller thread (threadIdx 0) also processes chunks
    while (true) {
        const size_t chunkStart = m_nextIndex.fetch_add(chunkSize, std::memory_order_relaxed);
        if (chunkStart >= end) {
            break;
        }
        const size_t chunkEnd = std::min(chunkStart + chunkSize, end);
        invoker(callable, chunkStart, chunkEnd, 0);
    }

    // Caller finished its work
    if (m_activeWorkers.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        // Caller was the last one to finish
        return;
    }

    // Wait for remaining worker threads to finish
    std::unique_lock<std::mutex> lock(m_mutex);
    m_doneCv.wait(lock, [&]() {
        return m_activeWorkers.load(std::memory_order_acquire) == 0;
    });
}

ThreadPool& ThreadPool::defaultPool() {
    static ThreadPool instance;
    return instance;
}

} // namespace horde::core
