#pragma once

#include <SDL3/SDL_gpu.h>

#include <filesystem>

namespace horde::gfx {

// An SDL_GPUTexture plus its dimensions. Move-only; releases the texture on
// destruction.
class Texture {
public:
    Texture() = default;
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    // Loads an image via SDL3_image and uploads it. Returns false and logs on
    // failure.
    bool loadFromFile(SDL_GPUDevice* device, const std::filesystem::path& path);

    // Creates an empty texture usable as a compute shader's read-write target
    // and as a sampled texture in the graphics pass.
    bool createStorageTarget(SDL_GPUDevice* device, Uint32 width, Uint32 height,
                             SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);

    void release();

    SDL_GPUTexture* handle() const {
        return m_texture;
    }

    Uint32 width() const {
        return m_width;
    }

    Uint32 height() const {
        return m_height;
    }

    explicit operator bool() const {
        return m_texture != nullptr;
    }

private:
    SDL_GPUDevice* m_device = nullptr;
    SDL_GPUTexture* m_texture = nullptr;
    Uint32 m_width = 0;
    Uint32 m_height = 0;
};

} // namespace horde::gfx
