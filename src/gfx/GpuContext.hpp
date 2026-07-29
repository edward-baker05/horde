#pragma once

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_video.h>

namespace horde::gfx {

// Owns the SDL_GPUDevice and its association with the window.
//
// SDL_GPU is a backend-agnostic API over Vulkan, D3D12 and Metal. We ask for
// every shader format we might ship so SDL can pick whichever backend is
// available; ShaderLoader then loads the matching binaries.
class GpuContext {
public:
    GpuContext() = default;
    ~GpuContext();

    GpuContext(const GpuContext&) = delete;
    GpuContext& operator=(const GpuContext&) = delete;

    // Creates the device and claims the window. Returns false and logs on failure.
    bool init(SDL_Window* window, bool enableDebug);
    void shutdown();

    SDL_GPUDevice* device() const {
        return m_device;
    }

    SDL_Window* window() const {
        return m_window;
    }

    // Format of the swapchain texture. Pipelines rendering to the screen must
    // declare this as their colour target format.
    SDL_GPUTextureFormat swapchainFormat() const;

    // Name of the backend actually chosen ("vulkan", "metal", "direct3d12").
    const char* driverName() const;

private:
    SDL_GPUDevice* m_device = nullptr;
    SDL_Window* m_window = nullptr;
};

} // namespace horde::gfx
