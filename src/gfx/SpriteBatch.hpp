#pragma once

#include <SDL3/SDL_gpu.h>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <vector>

namespace horde::gfx {

class ShaderLoader;

// One sprite instance. The layout must match `struct Sprite` in
// shaders/src/sprite.vert.hlsl byte for byte — HLSL packs to 16-byte
// boundaries, hence the explicit padding.
struct Sprite {
    glm::vec3 position{0.0f, 0.0f, 0.0f}; // z is the depth/layer
    float rotation = 0.0f;                // radians, counter-clockwise
    glm::vec2 size{1.0f, 1.0f};           // world-space width and height
    glm::vec2 padding{0.0f, 0.0f};
    glm::vec4 uv{0.0f, 0.0f, 1.0f, 1.0f}; // xy = top-left, zw = bottom-right
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

static_assert(sizeof(Sprite) == 64, "Sprite must match the HLSL struct layout");

// UV rectangle for one cell of a uniform grid atlas, in the xy/zw form Sprite
// expects. See drawCentered() below for placing a sprite by its centre rather
// than the corner this returns UVs for.
inline glm::vec4 atlasCell(int column, int row, int columns, int rows, float texelInset = 0.0001f) {
    const float cellWidth = 1.0f / static_cast<float>(columns);
    const float cellHeight = 1.0f / static_cast<float>(rows);

    return {static_cast<float>(column) * cellWidth + texelInset, static_cast<float>(row) * cellHeight + texelInset,
            static_cast<float>(column + 1) * cellWidth - texelInset,
            static_cast<float>(row + 1) * cellHeight - texelInset};
}

// Draws textured quads with one draw call per run of consecutive sprites that
// share a texture.
//
// There is no vertex buffer: the vertex shader builds each quad from
// SV_VertexID and reads transforms out of a storage buffer. Adding a sprite is
// therefore just appending to a vector. Because SV_VertexID includes the
// draw's first_vertex, a run starting at sprite N draws correctly with
// first_vertex = N * 6 and no rebinding beyond the texture.
//
// Sort by texture before drawing to keep the number of runs down.
class SpriteBatch {
public:
    SpriteBatch() = default;
    ~SpriteBatch();

    SpriteBatch(const SpriteBatch&) = delete;
    SpriteBatch& operator=(const SpriteBatch&) = delete;

    // Builds the pipeline against the given swapchain format. Returns false and
    // logs on failure.
    bool init(SDL_GPUDevice* device, const ShaderLoader& shaders, SDL_GPUTextureFormat colorTargetFormat,
              Uint32 maxSprites = 16384);
    void shutdown();

    // Clears the queue. Call once per frame before adding sprites.
    void begin();

    // Queues a sprite sampled from `texture`. Consecutive sprites sharing a
    // texture collapse into a single draw call.
    void draw(const Sprite& sprite, SDL_GPUTexture* texture);

    // Uploads the queue. Must be called on the command buffer OUTSIDE any
    // render pass, before flush().
    void upload(SDL_GPUCommandBuffer* commands);

    // Issues the draws inside an already-begun render pass.
    void flush(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const glm::mat4& viewProjection);

    size_t spriteCount() const {
        return m_sprites.size();
    }

    Uint32 capacity() const {
        return m_maxSprites;
    }

private:
    // A span of consecutive sprites sharing one texture: one draw call.
    struct Run {
        SDL_GPUTexture* texture = nullptr;
        Uint32 first = 0;
        Uint32 count = 0;
    };

    SDL_GPUDevice* m_device = nullptr;
    SDL_GPUGraphicsPipeline* m_pipeline = nullptr;
    SDL_GPUBuffer* m_spriteBuffer = nullptr;
    SDL_GPUTransferBuffer* m_transferBuffer = nullptr;
    SDL_GPUSampler* m_sampler = nullptr;

    std::vector<Sprite> m_sprites;
    std::vector<Run> m_runs;
    Uint32 m_maxSprites = 0;
    Uint32 m_uploadedCount = 0;
};

// Queues a sprite centred on `center` and rotated about that centre.
//
// Sprite::position is the quad's TOP-LEFT corner and Sprite::rotation turns the
// quad about that corner, so a centred shape must have its corner placed at
// center - R(rotation) * (size * 0.5). Every caller that thinks in centres —
// which is everything drawing a level — must go through here rather than
// repeating this trigonometry.
inline void drawCentered(SpriteBatch& batch, SDL_GPUTexture* texture, glm::vec2 center, glm::vec2 size, float rotation,
                         glm::vec4 uv, glm::vec4 color, float z = 0.0f) {
    const glm::vec2 half = size * 0.5f;
    const float c = std::cos(rotation);
    const float s = std::sin(rotation);
    const glm::vec2 offset{half.x * c - half.y * s, half.x * s + half.y * c};

    Sprite sprite;
    sprite.position = {center.x - offset.x, center.y - offset.y, z};
    sprite.rotation = rotation;
    sprite.size = size;
    sprite.uv = uv;
    sprite.color = color;

    batch.draw(sprite, texture);
}

} // namespace horde::gfx
