#include "sim/GpuReadback.hpp"

#include <SDL3/SDL_log.h>

namespace horde::sim {

GpuReadback::~GpuReadback() {
    shutdown();
}

bool GpuReadback::init(SDL_GPUDevice* device, Uint32 size, Uint32 slots) {
    shutdown();
    m_device = device;
    m_size = size;
    m_slots.resize(slots);
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = size;
    for (Slot& slot : m_slots) {
        slot.transfer = SDL_CreateGPUTransferBuffer(device, &info);
        if (slot.transfer == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GpuReadback: %s", SDL_GetError());
            return false;
        }
    }
    return true;
}

void GpuReadback::shutdown() {
    if (m_device == nullptr) {
        return;
    }
    for (Slot& slot : m_slots) {
        if (slot.fence) {
            // Shutting down: the one place waiting is right, so the transfer
            // buffer isn't released under a copy still running.
            SDL_WaitForGPUFences(m_device, true, &slot.fence, 1);
            SDL_ReleaseGPUFence(m_device, slot.fence);
        }
        if (slot.transfer) {
            SDL_ReleaseGPUTransferBuffer(m_device, slot.transfer);
        }
    }
    m_slots.clear();
    m_device = nullptr;
}

bool GpuReadback::request(SDL_GPUBuffer* source) {
    if (m_device == nullptr || source == nullptr) {
        return false;
    }
    Slot* free = nullptr;
    for (Slot& slot : m_slots) {
        if (slot.fence == nullptr) {
            free = &slot;
            break;
        }
    }
    if (free == nullptr) {
        return false;
    }

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(m_device);
    if (commands == nullptr) {
        return false;
    }
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    SDL_GPUBufferRegion src{};
    src.buffer = source;
    src.size = m_size;
    SDL_GPUTransferBufferLocation dst{};
    dst.transfer_buffer = free->transfer;
    SDL_DownloadFromGPUBuffer(copy, &src, &dst);
    SDL_EndGPUCopyPass(copy);
    free->fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    free->serial = m_nextSerial++;
    return free->fence != nullptr;
}

bool GpuReadback::poll(std::vector<std::byte>& out) {
    Slot* newest = nullptr;
    for (Slot& slot : m_slots) {
        if (slot.fence == nullptr || !SDL_QueryGPUFence(m_device, slot.fence)) {
            continue;
        }
        if (newest == nullptr || slot.serial > newest->serial) {
            if (newest != nullptr) {
                SDL_ReleaseGPUFence(m_device, newest->fence);
                newest->fence = nullptr;
            }
            newest = &slot;
        } else {
            SDL_ReleaseGPUFence(m_device, slot.fence);
            slot.fence = nullptr;
        }
    }
    if (newest == nullptr) {
        return false;
    }
    out.resize(m_size);
    const void* mapped = SDL_MapGPUTransferBuffer(m_device, newest->transfer, false);
    const bool ok = mapped != nullptr;
    if (ok) {
        SDL_memcpy(out.data(), mapped, m_size);
        SDL_UnmapGPUTransferBuffer(m_device, newest->transfer);
    }
    SDL_ReleaseGPUFence(m_device, newest->fence);
    newest->fence = nullptr;
    return ok;
}

} // namespace horde::sim
