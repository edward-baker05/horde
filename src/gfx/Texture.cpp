#include "gfx/Texture.hpp"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

#include <utility>

namespace horde::gfx {

Texture::~Texture() {
    release();
}

Texture::Texture(Texture&& other) noexcept
    : m_device(std::exchange(other.m_device, nullptr)), m_texture(std::exchange(other.m_texture, nullptr)),
      m_width(std::exchange(other.m_width, 0)), m_height(std::exchange(other.m_height, 0)) {}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        release();
        m_device = std::exchange(other.m_device, nullptr);
        m_texture = std::exchange(other.m_texture, nullptr);
        m_width = std::exchange(other.m_width, 0);
        m_height = std::exchange(other.m_height, 0);
    }

    return *this;
}

void Texture::release() {
    if (m_texture != nullptr && m_device != nullptr) {
        SDL_ReleaseGPUTexture(m_device, m_texture);
    }

    m_device = nullptr;
    m_texture = nullptr;
    m_width = 0;
    m_height = 0;
}

bool Texture::loadFromFile(SDL_GPUDevice* device, const std::filesystem::path& path) {
    release();

    SDL_Surface* loaded = IMG_Load(path.string().c_str());

    if (loaded == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "IMG_Load('%s') failed: %s", path.string().c_str(), SDL_GetError());
        return false;
    }

    // The sprite pipeline samples RGBA8, so normalise whatever came off disk.
    SDL_Surface* surface = loaded;

    if (surface->format != SDL_PIXELFORMAT_ABGR8888) {
        surface = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_ABGR8888);
        SDL_DestroySurface(loaded);

        if (surface == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "SDL_ConvertSurface failed: %s", SDL_GetError());
            return false;
        }
    }

    const Uint32 width = static_cast<Uint32>(surface->w);
    const Uint32 height = static_cast<Uint32>(surface->h);
    const Uint32 byteCount = width * height * 4;

    SDL_GPUTextureCreateInfo textureInfo{};
    textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
    textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    textureInfo.width = width;
    textureInfo.height = height;
    textureInfo.layer_count_or_depth = 1;
    textureInfo.num_levels = 1;

    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &textureInfo);

    if (texture == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "SDL_CreateGPUTexture failed: %s", SDL_GetError());
        SDL_DestroySurface(surface);
        return false;
    }

    // Pixels reach the GPU through a transfer buffer and a copy pass.
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = byteCount;

    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);

    if (transfer == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTexture(device, texture);
        SDL_DestroySurface(surface);
        return false;
    }

    // Copy row by row: SDL_Surface rows are padded to `pitch`, which is not
    // necessarily width * 4, but the transfer buffer must be tightly packed.
    auto* mapped = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device, transfer, false));
    const auto* pixels = static_cast<const Uint8*>(surface->pixels);

    for (Uint32 row = 0; row < height; ++row) {
        SDL_memcpy(mapped + row * width * 4, pixels + row * static_cast<size_t>(surface->pitch), width * 4);
    }

    SDL_UnmapGPUTransferBuffer(device, transfer);

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commands);

    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer;
    source.offset = 0;

    SDL_GPUTextureRegion destination{};
    destination.texture = texture;
    destination.w = width;
    destination.h = height;
    destination.d = 1;

    SDL_UploadToGPUTexture(copyPass, &source, &destination, false);
    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(commands);

    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_DestroySurface(surface);

    m_device = device;
    m_texture = texture;
    m_width = width;
    m_height = height;
    return true;
}

bool Texture::createStorageTarget(SDL_GPUDevice* device, Uint32 width, Uint32 height, SDL_GPUTextureFormat format) {
    release();

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = format;
    // COMPUTE_STORAGE_WRITE lets a compute shader write it; SAMPLER lets the
    // sprite pipeline read it back in the same frame.
    info.usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;

    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &info);

    if (texture == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "SDL_CreateGPUTexture (storage) failed: %s", SDL_GetError());
        return false;
    }

    m_device = device;
    m_texture = texture;
    m_width = width;
    m_height = height;
    return true;
}

} // namespace horde::gfx
