#pragma once

#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>

namespace horde::scene {
struct Services;
}

namespace horde::gfx {

// Draws units straight out of a GPU storage buffer of BaseUnitData, one quad
// per unit, with no per-unit CPU work.
//
// Shared by every physics system so that they all pay the same rendering cost
// and a benchmark compares their simulation, not their drawing.
class UnitRenderer {
public:
    UnitRenderer() = default;
    ~UnitRenderer();

    UnitRenderer(const UnitRenderer&) = delete;
    UnitRenderer& operator=(const UnitRenderer&) = delete;

    bool init(scene::Services& services);
    void shutdown();

    // Drawing between two physics steps: units are drawn `alpha` of the way
    // from `previous` (one float2 per unit) to their current position. Moves
    // longer than `maxJump` (a respawn) snap instead.
    struct Interpolation {
        SDL_GPUBuffer* previous = nullptr; // null: draw current positions as they are
        float alpha = 1.0f;
        float maxJump = 0.0f;
    };

    // `units` holds at least `count` BaseUnitData entries.
    void draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, SDL_GPUBuffer* units, Uint32 count,
              const glm::mat4& viewProjection, float unitSize, const Interpolation& blend) const;

    // Draws the units where they are, with no blending.
    void draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, SDL_GPUBuffer* units, Uint32 count,
              const glm::mat4& viewProjection, float unitSize) const {
        draw(commands, pass, units, count, viewProjection, unitSize, Interpolation{});
    }

private:
    SDL_GPUDevice* m_device = nullptr;
    SDL_GPUTexture* m_atlas = nullptr;
    SDL_GPUGraphicsPipeline* m_pipeline = nullptr;
    SDL_GPUSampler* m_sampler = nullptr;
};

} // namespace horde::gfx
