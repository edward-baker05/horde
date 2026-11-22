#include "gfx/RenderTarget.hpp"

#include <SDL3/SDL_log.h>

#include <glm/common.hpp>

namespace horde::gfx {

RenderTarget::~RenderTarget() {
    release();
}

void RenderTarget::release() {
    if (m_texture != nullptr) {
        SDL_ReleaseGPUTexture(m_device, m_texture);
        m_texture = nullptr;
    }
    m_size = glm::ivec2(0);
}

bool RenderTarget::resize(SDL_GPUDevice* device, SDL_GPUTextureFormat format, glm::ivec2 size) {
    size = glm::max(size, glm::ivec2(1));
    if (m_texture != nullptr && m_device == device && m_format == format && m_size == size) {
        return true;
    }

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = format;
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(size.x);
    info.height = static_cast<Uint32>(size.y);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;

    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &info);
    if (texture == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "RenderTarget: SDL_CreateGPUTexture failed: %s", SDL_GetError());
        return false;
    }

    release();
    m_device = device;
    m_texture = texture;
    m_format = format;
    m_size = size;
    return true;
}

} // namespace horde::gfx
