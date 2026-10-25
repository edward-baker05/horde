#pragma once

#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>

#include <cstdint>

#include "sim/PhysicsSystem.hpp"

namespace horde::scene {
struct Services;
}

namespace horde::sim {

class GpuSpatialGrid;
class GpuWalls;

// GPU collision resolution, as a stage any GPU physics system can run on its
// unit buffers after building a GpuSpatialGrid over them: units are separated
// from each other, then pushed out of walls.
//
// Each iteration is one Jacobi relaxation pass (unit_collide*.comp, chosen to
// match the grid's broadphase) reading one unit buffer and writing the other,
// so the caller owns a pair of buffers and resolve() reports which holds the
// result.
class GpuUnitCollisions {
public:
    GpuUnitCollisions() = default;
    ~GpuUnitCollisions();

    GpuUnitCollisions(const GpuUnitCollisions&) = delete;
    GpuUnitCollisions& operator=(const GpuUnitCollisions&) = delete;

    bool init(scene::Services& services, const CollisionSettings& settings, float diameter, glm::vec2 worldSize);
    void shutdown();

    // Runs settings.iterations passes starting from units[current]. Returns
    // the index (0 or 1) of the buffer holding the resolved units.
    std::uint32_t resolve(SDL_GPUCommandBuffer* commands, SDL_GPUBuffer* const units[2], std::uint32_t current,
                          std::uint32_t unitCount, const GpuSpatialGrid& grid, const GpuWalls& walls) const;

    const CollisionSettings& settings() const {
        return m_settings;
    }

private:
    SDL_GPUDevice* m_device = nullptr;
    SDL_GPUComputePipeline* m_pipeline = nullptr;
    CollisionSettings m_settings;
    float m_diameter = 5.0f;
    glm::vec2 m_worldMax{0.0f, 0.0f};
};

} // namespace horde::sim
