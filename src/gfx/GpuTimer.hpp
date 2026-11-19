#pragma once

#include <SDL3/SDL_gpu.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace horde::gfx {

// Times spans of GPU work without the main thread ever waiting for the GPU.
//
// SDL_GPU has no timestamp queries, so a span is bracketed by two fences
// instead: `begin`, from an empty command buffer submitted just before the
// work, signals when the GPU reaches it, and `end` signals when the work is
// done. A worker thread polls each fence in submission order and notes when it
// signals; the span's duration is the gap between the two. Polling is the
// lock-free SDL_QueryGPUFence, because waiting on a fence (SDL_WaitForGPUFences)
// holds the device's submit lock on D3D12, and would stall the main thread's
// next submit just as waiting there does.
//
// Results come back a frame or two late, which is fine for a rolling average.
// The cost is the worker thread's polling, on the CPU, while spans are in
// flight; the GPU runs exactly as it would untimed.
class GpuTimer {
public:
    GpuTimer() = default;
    ~GpuTimer();

    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;

    void start(SDL_GPUDevice* device);
    // Joins the worker and releases every fence it still holds.
    void stop();

    // Times the GPU work between two fences, which must come from consecutive
    // submissions. Takes ownership of both.
    void track(SDL_GPUFence* begin, SDL_GPUFence* end);

    // Appends, in nanoseconds, every span finished since the last call, and
    // releases the fences of those spans. Main thread only, like any
    // SDL_ReleaseGPUFence.
    void collect(std::vector<Uint64>& out);

private:
    struct Span {
        SDL_GPUFence* begin = nullptr;
        SDL_GPUFence* end = nullptr;
    };

    void run();
    // Polls until `fence` signals, returning when it did; 0 if stopping first.
    Uint64 signalledAt(SDL_GPUFence* fence);

    SDL_GPUDevice* m_device = nullptr;
    std::thread m_thread;
    std::atomic<bool> m_stopping{false};

    // Guarded by m_mutex. The worker times m_pending.front() and pops it only
    // when done, so stop() still finds and releases a span cut off mid-way.
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<Span> m_pending;
    std::vector<Uint64> m_durations;
    std::vector<SDL_GPUFence*> m_spent;
};

} // namespace horde::gfx
