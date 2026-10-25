#include "sim/UnitBuffers.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>

namespace horde::sim {

SDL_GPUBuffer* createUnitBuffer(SDL_GPUDevice* device, std::uint32_t count, SDL_GPUBufferUsageFlags usage) {
    SDL_GPUBufferCreateInfo info{};
    info.usage = usage;
    info.size = static_cast<Uint32>(sizeof(BaseUnitData) * std::max<std::uint32_t>(count, 1));

    SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(device, &info);
    if (buffer == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create unit buffer (%u units): %s", count, SDL_GetError());
    }
    return buffer;
}

bool uploadUnitsNow(SDL_GPUDevice* device, const BaseUnitData* units, std::uint32_t count,
                    SDL_GPUBuffer* const* targets, int targetCount) {
    if (count == 0) {
        return true;
    }

    const Uint32 size = static_cast<Uint32>(sizeof(BaseUnitData) * count);
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = size;

    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
    if (transfer == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create unit upload buffer: %s", SDL_GetError());
        return false;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    SDL_memcpy(mapped, units, size);
    SDL_UnmapGPUTransferBuffer(device, transfer);

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commands);

    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = transfer;

    for (int i = 0; i < targetCount; ++i) {
        SDL_GPUBufferRegion dst{};
        dst.buffer = targets[i];
        dst.size = size;
        SDL_UploadToGPUBuffer(copyPass, &src, &dst, false);
    }

    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(commands);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return true;
}

bool downloadUnits(SDL_GPUDevice* device, SDL_GPUBuffer* source, std::uint32_t count,
                   std::vector<BaseUnitData>& out) {
    out.resize(count);
    if (count == 0) {
        return true;
    }

    const Uint32 size = static_cast<Uint32>(sizeof(BaseUnitData) * count);
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transferInfo.size = size;

    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
    if (transfer == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create unit download buffer: %s", SDL_GetError());
        return false;
    }

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commands);

    SDL_GPUBufferRegion src{};
    src.buffer = source;
    src.size = size;

    SDL_GPUTransferBufferLocation dst{};
    dst.transfer_buffer = transfer;

    SDL_DownloadFromGPUBuffer(copyPass, &src, &dst);
    SDL_EndGPUCopyPass(copyPass);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    bool ok = fence != nullptr && SDL_WaitForGPUFences(device, true, &fence, 1);
    if (fence) {
        SDL_ReleaseGPUFence(device, fence);
    }

    if (ok) {
        const void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
        SDL_memcpy(out.data(), mapped, size);
        SDL_UnmapGPUTransferBuffer(device, transfer);
    }

    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return ok;
}

UnitStreamer::~UnitStreamer() {
    shutdown();
}

bool UnitStreamer::init(SDL_GPUDevice* device, std::uint32_t capacity) {
    m_device = device;
    m_capacity = capacity;

    m_buffer = createUnitBuffer(device, capacity, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);

    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<Uint32>(sizeof(BaseUnitData) * std::max<std::uint32_t>(capacity, 1));
    m_transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);

    if (m_buffer == nullptr || m_transfer == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create unit streaming buffers: %s", SDL_GetError());
        return false;
    }
    return true;
}

void UnitStreamer::shutdown() {
    if (m_device == nullptr) {
        return;
    }
    if (m_transfer) {
        SDL_ReleaseGPUTransferBuffer(m_device, m_transfer);
        m_transfer = nullptr;
    }
    if (m_buffer) {
        SDL_ReleaseGPUBuffer(m_device, m_buffer);
        m_buffer = nullptr;
    }
    m_device = nullptr;
}

void UnitStreamer::upload(SDL_GPUCommandBuffer* commands, const BaseUnitData* units, std::uint32_t count) {
    count = std::min(count, m_capacity);
    if (!m_buffer || !m_transfer || count == 0) {
        return;
    }

    const Uint32 size = static_cast<Uint32>(sizeof(BaseUnitData) * count);

    // Cycling both buffers lets this frame's upload proceed while the GPU is
    // still drawing from last frame's data.
    void* mapped = SDL_MapGPUTransferBuffer(m_device, m_transfer, true);
    SDL_memcpy(mapped, units, size);
    SDL_UnmapGPUTransferBuffer(m_device, m_transfer);

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commands);

    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = m_transfer;

    SDL_GPUBufferRegion dst{};
    dst.buffer = m_buffer;
    dst.size = size;

    SDL_UploadToGPUBuffer(copyPass, &src, &dst, true);
    SDL_EndGPUCopyPass(copyPass);
}

} // namespace horde::sim
