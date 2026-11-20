#pragma once

#include <SDL3/SDL_gpu.h>

#include <glm/vec2.hpp>

#include "gfx/RenderTarget.hpp"

namespace horde::gfx {

class ShaderLoader;

// A separable gaussian blur of a texture: 9 taps horizontally, then vertically,
// at half resolution (the blur hides the lost detail and costs a quarter).
// One Blur per source, as the result lives in its own targets until the frame's
// swapchain pass samples it.
class Blur {
public:
    Blur() = default;
    ~Blur();

    Blur(const Blur&) = delete;
    Blur& operator=(const Blur&) = delete;

    bool init(SDL_GPUDevice* device, const ShaderLoader& shaders, SDL_GPUTextureFormat format);
    void shutdown();

    // Sizes the targets for a source of `size` px. Call it when the source is
    // resized so texture() stays the same pointer a composite queued earlier in
    // the frame refers to. A no-op when unchanged.
    bool resize(glm::ivec2 size);

    // Blurs `source` (of `size` px) in two render passes, so it must come after
    // every copy pass of the frame. Returns the blurred texture, or null on failure.
    SDL_GPUTexture* apply(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* source, glm::ivec2 size);

    // The blurred result; valid from resize(), written by apply().
    SDL_GPUTexture* texture() const { return m_vertical.texture(); }

private:
    bool pass(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* source, RenderTarget& target, glm::vec2 direction);

    SDL_GPUDevice* m_device = nullptr;
    SDL_GPUTextureFormat m_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUGraphicsPipeline* m_pipeline = nullptr;
    SDL_GPUSampler* m_sampler = nullptr;
    RenderTarget m_horizontal;
    RenderTarget m_vertical;
};

} // namespace horde::gfx
