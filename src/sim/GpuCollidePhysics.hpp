#pragma once

#include <optional>
#include <random>

#include "gfx/UnitRenderer.hpp"
#include "sim/GpuSpatialGrid.hpp"
#include "sim/GpuUnitCollisions.hpp"
#include "sim/GpuWalls.hpp"
#include "sim/PhysicsSystem.hpp"

namespace horde::sim {

// Units stream in at the level's spawns, steer toward the nearest exit, push
// each other apart and slide along walls, all on the GPU; units that reach an
// exit re-enter at a spawn.
//
// Per frame, as position-based dynamics:
//   0. (CPU)              upload any units due to spawn this frame
//   1. unit_integrate     steer toward the nearest exit, predict new positions
//   2. GpuSpatialGrid     bucket the predicted positions (the broadphase)
//   3. GpuUnitCollisions  N relaxation passes, each separating units then
//                         pushing them out of walls
//   4. unit_finalize      out of walls once more, clamp to the level,
//                         velocity = actual displacement / dt, respawn
//                         units that reached an exit
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

    bool supportsFlow() const override {
        return true;
    }

    // Units in the level so far: grows while spawns stream in, up to capacity.
    std::uint32_t unitCount() const override {
        return m_unitCount;
    }

private:
    void spawnPending(SDL_GPUCommandBuffer* commands);

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

    // Streaming spawns: units [m_unitCount, m_capacity) are still to come.
    std::uint32_t m_capacity = 0;
    SDL_GPUTransferBuffer* m_spawnTransfer = nullptr;
    std::mt19937 m_spawnRng;
    double m_spawnDue = 0.0;
    std::uint32_t m_frame = 0;

    SimConfig m_config;
    std::uint32_t m_unitCount = 0;
    float m_dt = 0.0f;
};

} // namespace horde::sim
