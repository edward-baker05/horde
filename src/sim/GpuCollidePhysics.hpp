#pragma once

#include "gfx/UnitRenderer.hpp"
#include "sim/GpuSpatialGrid.hpp"
#include "sim/GpuUnitCollisions.hpp"
#include "sim/PhysicsSystem.hpp"

namespace horde::sim {

// Units steer toward the level's exit and push each other apart, all on the GPU.
//
// Per frame, as position-based dynamics:
//   1. unit_integrate   steer toward the goal and predict new positions
//   2. GpuSpatialGrid   bucket the predicted positions (the broadphase)
//   3. GpuUnitCollisions  N relaxation passes separating overlapping units
//   4. unit_finalize    clamp to the level, velocity = actual displacement / dt
//
// Each stage is its own piece so later systems can reuse or swap one: walls
// would be another correction pass beside step 3, a faster sort lives inside
// the grid, and steering could read the grid for local avoidance.
class GpuCollidePhysics : public PhysicsSystem {
public:
    ~GpuCollidePhysics() override;

    bool init(scene::Services& services, const SimConfig& config) override;
    void shutdown() override;
    void update(float dt) override;
    void compute(SDL_GPUCommandBuffer* commands) override;
    void renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                    const glm::mat4& viewProjection) override;
    void debugUi() override;
    bool readUnits(std::vector<BaseUnitData>& out) override;

    std::uint32_t unitCount() const override {
        return m_unitCount;
    }

private:
    SDL_GPUDevice* m_device = nullptr;
    gfx::UnitRenderer m_renderer;
    GpuSpatialGrid m_grid;
    GpuUnitCollisions m_collisions;

    SDL_GPUComputePipeline* m_integratePipeline = nullptr;
    SDL_GPUComputePipeline* m_finalizePipeline = nullptr;

    // Collision passes ping-pong between these; m_current holds the latest units.
    SDL_GPUBuffer* m_units[2] = {nullptr, nullptr};
    SDL_GPUBuffer* m_prevPositions = nullptr;
    std::uint32_t m_current = 0;

    SimConfig m_config;
    std::uint32_t m_unitCount = 0;
    float m_dt = 0.0f;
};

} // namespace horde::sim
