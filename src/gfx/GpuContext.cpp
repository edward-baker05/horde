#include "gfx/GpuContext.hpp"

#include <SDL3/SDL_log.h>

namespace horde::gfx {

GpuContext::~GpuContext() {
    shutdown();
}

bool GpuContext::init(SDL_Window* window, bool enableDebug) {
    // Requesting all three formats lets SDL choose any available backend. On
    // Windows without committed DXIL this means the Vulkan backend is used,
    // which is fine — see cmake/Shaders.cmake for why DXIL is Windows-only.
    const SDL_GPUShaderFormat formats =
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_DXIL;

    m_device = SDL_CreateGPUDevice(formats, enableDebug, nullptr);

    if (m_device == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return false;
    }

    if (!SDL_ClaimWindowForGPUDevice(m_device, window)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        SDL_DestroyGPUDevice(m_device);
        m_device = nullptr;
        return false;
    }

    m_window = window;

    SDL_Log("GPU backend: %s", driverName());
    return true;
}

void GpuContext::shutdown() {
    if (m_device == nullptr) {
        return;
    }

    if (m_window != nullptr) {
        SDL_ReleaseWindowFromGPUDevice(m_device, m_window);
        m_window = nullptr;
    }

    SDL_DestroyGPUDevice(m_device);
    m_device = nullptr;
}

SDL_GPUTextureFormat GpuContext::swapchainFormat() const {
    return SDL_GetGPUSwapchainTextureFormat(m_device, m_window);
}

const char* GpuContext::driverName() const {
    const char* name = SDL_GetGPUDeviceDriver(m_device);
    return name != nullptr ? name : "unknown";
}

} // namespace horde::gfx
