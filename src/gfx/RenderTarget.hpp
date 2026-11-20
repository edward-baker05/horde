#pragma once

#include <SDL3/SDL_gpu.h>

#include <glm/vec2.hpp>

namespace horde::gfx {

// An offscreen colour texture a render pass can draw into and a later pass can
// sample. The device shell renders each phone/tablet screen into one of these
// and composites it onto the swapchain.
class RenderTarget {
public:
    RenderTarget() = default;
    ~RenderTarget();

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    // (Re)creates the texture; a no-op when the size and format are unchanged.
    // The old texture is released straight away: SDL keeps it alive until any
    // command buffer still using it has finished.
    bool resize(SDL_GPUDevice* device, SDL_GPUTextureFormat format, glm::ivec2 size);
    void release();

    SDL_GPUTexture* texture() const { return m_texture; }
    glm::ivec2 size() const { return m_size; }

private:
    SDL_GPUDevice* m_device = nullptr;
    SDL_GPUTexture* m_texture = nullptr;
    SDL_GPUTextureFormat m_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    glm::ivec2 m_size{0};
};

} // namespace horde::gfx
