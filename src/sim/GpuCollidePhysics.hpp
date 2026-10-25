#pragma once

#include <optional>

#include "gfx/UnitRenderer.hpp"
#include "sim/GpuSpatialGrid.hpp"
#include "sim/GpuUnitCollisions.hpp"
#include "sim/GpuWalls.hpp"
#include "sim/PhysicsSystem.hpp"

namespace horde::sim {

// Units steer toward the level's exit, push each other apart and slide along
// walls, all on the GPU.
//
// Per frame, as position-based dynamics:
//   1. unit_integrate     steer toward the goal and predict new positions
//   2. GpuSpatialGrid     bucket the predicted positions (the broadphase)
//   3. GpuUnitCollisions  N relaxation passes, each separating units then
//                         pushing them out of walls
//   4. unit_finalize      out of walls once more, clamp to the level,
//                         velocity = actual displacement / dt
//
// Each stage is its own piece so later systems can reuse or swap one: the
// broadphase method is a setting, and steering could read the grid for local
// avoidance.
class GpuCollidePhysics : public PhysicsSystem {
public:
    // `broadphase` overrides SimConfig::collision.broadphase, for registry
    // entries that pin one method so benchmarks can compare them.
    explicit GpuCollidePhysics(std::optional<Broadphase> broadphase = std::nullopt) : m_forcedBroadphase(broadphase) {}
    ~GpuCollidePhysics() override;

    bool init(scene::Services& services, const SimConfig& config) override;
    void shutdown() override;
    void update(float dt) override;
    void compute(SDL_GPUCommandBuffer* commands) override;
    void renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                    const glm::mat4& viewProjection) override;
    void debugUi() override;
    bool readUnits(std::vector<BaseUnitData>& out) override;

    const CollisionSettings* collisionSettings() const override {
        return &m_config.collision;
    }

    std::uint32_t unitCount() const override {
        return m_unitCount;
    }

private:
    SDL_GPUDevice* m_device = nullptr;
    gfx::UnitRenderer m_renderer;
    GpuSpatialGrid m_grid;
    GpuUnitCollisions m_collisions;
    GpuWalls m_walls;
    std::optional<Broadphase> m_forcedBroadphase;

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
