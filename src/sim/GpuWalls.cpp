#include "sim/GpuWalls.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>

#include "sim/UnitBuffers.hpp"
#include "sim/WallColliders.hpp"

namespace horde::sim {

namespace {

// Creates a read-only storage buffer holding `bytes`, or one zeroed dummy
// element of `minimum` bytes when there is nothing to hold.
SDL_GPUBuffer* createFilled(SDL_GPUDevice* device, const void* data, std::uint32_t bytes, std::uint32_t minimum) {
    static const std::uint8_t zeros[64] = {};

    SDL_GPUBufferCreateInfo info{};
    info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    info.size = std::max(bytes, minimum);
    SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(device, &info);
    if (buffer == nullptr) {
        return nullptr;
    }

    const bool ok = bytes > 0 ? uploadBytesNow(device, data, bytes, buffer) : uploadBytesNow(device, zeros, minimum, buffer);
    if (!ok) {
        SDL_ReleaseGPUBuffer(device, buffer);
        return nullptr;
    }
    return buffer;
}

} // namespace

GpuWalls::~GpuWalls() {
    shutdown();
}

bool GpuWalls::init(SDL_GPUDevice* device, const WallColliders* walls) {
    m_device = device;

    const bool any = walls != nullptr && !walls->empty();
    m_count = any ? static_cast<std::uint32_t>(walls->primitives().size()) : 0;
    m_binDim = any ? walls->binDim() : glm::uvec2(1, 1);
    m_binSize = any ? walls->binSize() : 1.0f;

    const auto bytesOf = [](const auto& vector) {
        return static_cast<std::uint32_t>(vector.size() * sizeof(vector[0]));
    };

    if (any) {
        m_buffers[0] = createFilled(device, walls->primitives().data(), bytesOf(walls->primitives()),
                                    sizeof(WallPrimitive));
        m_buffers[1] = createFilled(device, walls->bins().data(), bytesOf(walls->bins()), sizeof(glm::uvec2));
        m_buffers[2] = createFilled(device, walls->binIndices().data(), bytesOf(walls->binIndices()),
                                    sizeof(std::uint32_t));
    } else {
        m_buffers[0] = createFilled(device, nullptr, 0, sizeof(WallPrimitive));
        m_buffers[1] = createFilled(device, nullptr, 0, sizeof(glm::uvec2));
        m_buffers[2] = createFilled(device, nullptr, 0, sizeof(std::uint32_t));
    }

    if (!m_buffers[0] || !m_buffers[1] || !m_buffers[2]) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "GpuWalls: failed to create wall buffers: %s", SDL_GetError());
        return false;
    }
    return true;
}

void GpuWalls::shutdown() {
    if (m_device == nullptr) {
        return;
    }
    for (SDL_GPUBuffer*& buffer : m_buffers) {
        if (buffer) {
            SDL_ReleaseGPUBuffer(m_device, buffer);
            buffer = nullptr;
        }
    }
    m_device = nullptr;
}

} // namespace horde::sim
