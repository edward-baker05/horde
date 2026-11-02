#include "gfx/GpuTimer.hpp"

#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_timer.h>

namespace horde::gfx {

GpuTimer::~GpuTimer() {
    stop();
}

void GpuTimer::start(SDL_GPUDevice* device) {
    stop();
    m_device = device;
    m_stopping = false;
    m_thread = std::thread([this] { run(); });
}

void GpuTimer::stop() {
    if (m_thread.joinable()) {
        {
            std::lock_guard lock(m_mutex);
            m_stopping = true;
        }
        m_wake.notify_one();
        m_thread.join();
    }

    if (m_device == nullptr) {
        return;
    }
    for (const Span& span : m_pending) {
        SDL_ReleaseGPUFence(m_device, span.begin);
        SDL_ReleaseGPUFence(m_device, span.end);
    }
    for (SDL_GPUFence* fence : m_spent) {
        SDL_ReleaseGPUFence(m_device, fence);
    }
    m_pending.clear();
    m_spent.clear();
    m_durations.clear();
    m_device = nullptr;
}

void GpuTimer::track(SDL_GPUFence* begin, SDL_GPUFence* end) {
    {
        std::lock_guard lock(m_mutex);
        m_pending.push_back({begin, end});
    }
    m_wake.notify_one();
}

void GpuTimer::collect(std::vector<Uint64>& out) {
    std::vector<SDL_GPUFence*> spent;
    {
        std::lock_guard lock(m_mutex);
        out.insert(out.end(), m_durations.begin(), m_durations.end());
        m_durations.clear();
        spent.swap(m_spent);
    }
    for (SDL_GPUFence* fence : spent) {
        SDL_ReleaseGPUFence(m_device, fence);
    }
}

Uint64 GpuTimer::signalledAt(SDL_GPUFence* fence) {
    // Spin briefly between checks, then give the core away: precise to a few
    // microseconds without starving anything else that wants to run.
    while (!SDL_QueryGPUFence(m_device, fence)) {
        if (m_stopping) {
            return 0;
        }
        for (int i = 0; i < 32; ++i) {
            SDL_CPUPauseInstruction();
        }
        std::this_thread::yield();
    }
    return SDL_GetTicksNS();
}

void GpuTimer::run() {
    for (;;) {
        Span span;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stopping || !m_pending.empty(); });
            if (m_stopping) {
                return;
            }
            span = m_pending.front();
        }

        const Uint64 began = signalledAt(span.begin);
        const Uint64 ended = began != 0 ? signalledAt(span.end) : 0;
        if (ended == 0) {
            return; // stopping: stop() releases the span, still at the front
        }

        std::lock_guard lock(m_mutex);
        m_pending.pop_front();
        m_durations.push_back(ended - began);
        m_spent.push_back(span.begin);
        m_spent.push_back(span.end);
    }
}

} // namespace horde::gfx
