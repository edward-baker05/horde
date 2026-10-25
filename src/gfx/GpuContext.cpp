#include "gfx/GpuContext.hpp"

#include <SDL3/SDL_log.h>

namespace horde::gfx {

GpuContext::~GpuContext() {
    shutdown();
}

bool GpuContext::init(SDL_Window* window, bool enableDebug, bool preferLowPower) {
    // Requesting all three formats lets SDL choose any available backend. On
    // Windows without committed DXIL this means the Vulkan backend is used,
    // which is fine — see cmake/Shaders.cmake for why DXIL is Windows-only.
    const SDL_GPUShaderFormat formats =
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_DXIL;

    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, enableDebug);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN, preferLowPower);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, (formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, (formats & SDL_GPU_SHADERFORMAT_MSL) != 0);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, (formats & SDL_GPU_SHADERFORMAT_DXIL) != 0);
    m_device = SDL_CreateGPUDeviceWithProperties(props);
    SDL_DestroyProperties(props);

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

    SDL_Log("GPU backend: %s, device: %s", driverName(), deviceName());
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

const char* GpuContext::deviceName() const {
    if (m_device == nullptr) {
        return "unknown";
    }
    const char* name = SDL_GetStringProperty(SDL_GetGPUDeviceProperties(m_device), SDL_PROP_GPU_DEVICE_NAME_STRING, nullptr);
    return name != nullptr ? name : "unknown";
}

} // namespace horde::gfx
