#pragma once

#include <SDL3/SDL_gpu.h>

#include <cstddef>
#include <vector>

namespace horde::sim {

// Copies a small GPU buffer back to the CPU without ever waiting for it.
//
// Each request records the copy in a command buffer of its own and submits it
// at once, so it sees everything submitted before it; poll() later picks up
// whichever copies have finished. A few copies can be in flight, so a
// request every frame keeps a steady stream of reports at a frame or two of
// latency, where readHealth-style downloads stall the frame.
class GpuReadback {
public:
    GpuReadback() = default;
    ~GpuReadback();

    GpuReadback(const GpuReadback&) = delete;
    GpuReadback& operator=(const GpuReadback&) = delete;

    bool init(SDL_GPUDevice* device, Uint32 size, Uint32 slots = 3);
    void shutdown();

    // Downloads `size` bytes from the start of `source`. False when every
    // slot is still in flight, or on failure.
    bool request(SDL_GPUBuffer* source);

    // The newest copy that finished since the last call, if any. Older
    // finished copies are dropped. Never waits.
    bool poll(std::vector<std::byte>& out);

private:
    struct Slot {
        SDL_GPUTransferBuffer* transfer = nullptr;
        SDL_GPUFence* fence = nullptr; // null: free
        unsigned long long serial = 0;
    };

    SDL_GPUDevice* m_device = nullptr;
    Uint32 m_size = 0;
    std::vector<Slot> m_slots;
    unsigned long long m_nextSerial = 1;
};

} // namespace horde::sim
