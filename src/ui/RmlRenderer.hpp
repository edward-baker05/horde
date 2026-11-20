#pragma once

#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>

#include <RmlUi/Core/RenderInterface.h>

#include <array>
#include <vector>

#include "ui/DeviceLayout.hpp"

namespace horde::gfx {
class ShaderLoader;
}

namespace horde::ui {

// RmlUi's RenderInterface on SDL_GPU.
//
// SDL_GPU forbids uploads inside a render pass, but RmlUi compiles geometry and
// glyph textures while Context::Render() runs. So a frame is split in two:
// beginRecord() before any pass lets Context::Render() upload through copy
// passes while this class only notes the draws; draw() then replays those
// draws inside the target's render pass.
//
// Clip masks, layers and filters are not implemented; RmlUi falls back to
// scissor-only clipping and ignores the features that need them.
class RmlRenderer : public Rml::RenderInterface {
public:
    RmlRenderer() = default;
    ~RmlRenderer() override;

    bool init(SDL_GPUDevice* device, const gfx::ShaderLoader& shaders, SDL_GPUTextureFormat targetFormat);
    void shutdown();

    // Once per frame, before any beginRecord(): frees what RmlUi released
    // since last frame. Done here rather than per record so that one device's
    // recorded draws never lose a buffer when another device records.
    void beginFrame();

    // Call before Context::Render(), outside any render pass. Uploads made by
    // RmlUi until endRecord() go on `commands`; the draws are kept in `kind`'s
    // own list, so every device can record before any of them draws.
    void beginRecord(DeviceKind kind, SDL_GPUCommandBuffer* commands, glm::ivec2 targetSize);

    // Ends the upload window so a stale command buffer is never touched, even
    // if the frame never reaches draw().
    void endRecord();

    // Replays `kind`'s recorded draws into `pass`.
    void draw(DeviceKind kind, SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* commands);

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

    Rml::TextureHandle LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i sourceDimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;

    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

    void SetTransform(const Rml::Matrix4f* transform) override;

private:
    struct Geometry {
        SDL_GPUBuffer* vertices = nullptr;
        SDL_GPUBuffer* indices = nullptr;
        Uint32 indexCount = 0;
    };

    struct Texture {
        SDL_GPUTexture* texture = nullptr;
    };

    struct DrawCommand {
        SDL_GPUBuffer* vertices;
        SDL_GPUBuffer* indices;
        Uint32 indexCount;
        SDL_GPUTexture* texture;
        glm::vec2 translation;
        glm::mat4 transform;
        SDL_Rect scissor;
    };

    // Uploads on the recording command buffer, or on a throwaway one submitted
    // at once when RmlUi loads something outside a frame (document load).
    template<typename Fn>
    void withCopyPass(Fn&& fn);
    SDL_GPUBuffer* uploadBuffer(SDL_GPUBufferUsageFlags usage, const void* data, Uint32 size);
    Rml::TextureHandle createTexture(const Rml::byte* rgba, int width, int height);
    void flushReleases();

    SDL_GPUDevice* m_device = nullptr;
    SDL_GPUGraphicsPipeline* m_pipeline = nullptr;
    SDL_GPUSampler* m_sampler = nullptr;
    Texture* m_white = nullptr;

    SDL_GPUCommandBuffer* m_commands = nullptr;
    glm::ivec2 m_targetSize{0}; // of the list being recorded
    std::array<glm::ivec2, 3> m_listSizes{};
    int m_recording = 0;

    // Draw state as RmlUi sets it; captured into each DrawCommand.
    bool m_scissorEnabled = false;
    SDL_Rect m_scissor{};
    glm::mat4 m_transform{1.0f};

    std::array<std::vector<DrawCommand>, 3> m_lists;
    // Releases wait for the next beginFrame so a draw already recorded this
    // frame never points at a freed buffer.
    std::vector<SDL_GPUBuffer*> m_pendingBuffers;
    std::vector<SDL_GPUTexture*> m_pendingTextures;
};

} // namespace horde::ui
