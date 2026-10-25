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

    // `units` holds at least `count` BaseUnitData entries.
    void draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, SDL_GPUBuffer* units, Uint32 count,
              const glm::mat4& viewProjection, float unitSize) const;

private:
    SDL_GPUDevice* m_device = nullptr;
    SDL_GPUTexture* m_atlas = nullptr;
    SDL_GPUGraphicsPipeline* m_pipeline = nullptr;
    SDL_GPUSampler* m_sampler = nullptr;
};

} // namespace horde::gfx
