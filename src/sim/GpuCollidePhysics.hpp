#pragma once

#include <random>

#include "gfx/UnitRenderer.hpp"
#include "sim/GpuSpatialGrid.hpp"
#include "sim/GpuUnitCollisions.hpp"
#include "sim/GpuUnitReorder.hpp"
#include "sim/GpuWalls.hpp"
#include "sim/PhysicsSystem.hpp"
#include "sim/TurretEffects.hpp"

namespace horde::sim {

// Units stream in at the level's spawns, steer toward the nearest exit, push
// each other apart and slide along walls, all on the GPU; units that reach an
// exit re-enter at a spawn.
//
// Per frame, as position-based dynamics:
//   0. (CPU)              upload any units due to spawn this frame
//   1. unit_integrate     steer toward the nearest exit, or along a field
//                         region's flow inside one; predict new positions,
//                         then push them by any active debug explosions
//   2. GpuSpatialGrid     bucket the predicted positions (the broadphase)
//      turret_aim         aim any mortars firing this step
//      GpuUnitReorder     (CollisionSettings::reorderUnits) put every
//                         per-unit buffer in grid order
//      turret_fire        aim any guns firing this step, and load their bullets
//      bullet_step        move bullets, letting them hit units
//   3. GpuUnitCollisions  N relaxation passes, each separating units then
//                         pushing them out of walls
//   4. unit_finalize      out of walls once more, clamp to the level,
//                         velocity = actual displacement / dt, respawn
//                         units that reached an exit
//
// Each stage is its own piece so later systems can reuse or swap one, and
// steering could read the grid for local avoidance.
class GpuCollidePhysics : public PhysicsSystem {
public:
    GpuCollidePhysics() = default;
    ~GpuCollidePhysics() override;

    bool init(scene::Services& services, const SimConfig& config) override;
    void shutdown() override;
    void update(float dt) override;
    void compute(SDL_GPUCommandBuffer* commands) override;
    void renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const glm::mat4& viewProjection,
                    glm::vec2 viewportPx, float alpha) override;
    void debugUi() override;
    bool readUnits(std::vector<BaseUnitData>& out) override;
    bool readHealth(std::vector<float>& out) override;

    bool tally(UnitTally& out) const override {
        out = m_tally;
        return true;
    }

    std::uint32_t pendingSpawns() const override {
        return m_capacity - m_unitCount;
    }

    const CollisionSettings* collisionSettings() const override {
        return &m_config.collision;
    }

    bool supportsFlow() const override {
        return true;
    }

    bool supportsExplosions() const override {
        return true;
    }

    bool addExplosion(const Explosion& explosion) override;
    std::vector<Explosion> activeExplosions() const override;

    void setFlingDrag(float perSecond) override {
        m_flingDrag = perSecond;
    }

    bool supportsRounds() const override {
        return true;
    }

    void setSpawnRate(float perSecond) override {
        m_config.flow.spawnRate = perSecond;
    }

    bool setTurrets(const std::vector<logic::Turret>& turrets) override;

    const TurretEffects* turretEffects() const override {
        return &m_turrets;
    }

    bool setCollisionPasses(std::uint32_t passes) override {
        m_config.collision.iterations = passes;
        m_collisions.setIterations(passes);
        return true;
    }

    // Units in the level so far: grows while spawns stream in, up to capacity.
    std::uint32_t unitCount() const override {
        return m_unitCount;
    }

private:
    void spawnPending(SDL_GPUCommandBuffer* commands);
    // Picks up the last feedback download if it has landed, and starts the next.
    void sampleFeedback();
    // Re-uploads the walls as the level's own plus the blockers still standing.
    void rebuildWalls();
    // Uploads what each turret slot's hits do to units.
    void uploadStatusTable();
    // Records turret_aim for this step's mortar shots, on the grid just built.
    void aimMortars(SDL_GPUCommandBuffer* commands, const std::vector<AimRequest>& aims);
    // Records turret_fire for this step's gunshots, after the reorder.
    void fireGuns(SDL_GPUCommandBuffer* commands, const std::vector<FireRequest>& fires);
    // Records bullet_step, after fireGuns.
    void stepBullets(SDL_GPUCommandBuffer* commands);
    // Starting health for `count` new units: all full.
    std::vector<float> startingHealth(std::uint32_t count) const;

    SDL_GPUDevice* m_device = nullptr;
    gfx::UnitRenderer m_renderer;

    // How units are drawn; changed from the debug UI.
    struct SpriteSettings {
        bool enabled = true;
        gfx::UnitRenderer::Species species = gfx::UnitRenderer::Species::Humans;
        gfx::UnitRenderer::Lod lod = gfx::UnitRenderer::Lod::Auto;
        gfx::UnitRenderer::LodThresholds thresholds;
        bool rotate = true;
        int directionSteps = 16; // 0 = any angle
        bool integerScale = false;
    } m_sprites;

    GpuSpatialGrid m_grid;
    GpuUnitCollisions m_collisions;
    GpuUnitReorder m_reorder;
    GpuWalls m_walls;

    SDL_GPUComputePipeline* m_integratePipeline = nullptr;
    // Whether m_grid holds a step's grid yet, for unit_integrate's density map.
    bool m_gridBuilt = false;
    SDL_GPUComputePipeline* m_finalizePipeline = nullptr;

    // Collision passes ping-pong between these; m_current holds the latest units.
    SDL_GPUBuffer* m_units[2] = {nullptr, nullptr};
    SDL_GPUBuffer* m_prevPositions = nullptr;
    // Hit points per unit, 0 to maxHp. Not ping-ponged: only positions are.
    SDL_GPUBuffer* m_health = nullptr;
    // Each unit's stable ID, which stays with it when units are reordered.
    // Slot i starts out holding i, so a unit spawned into slot i gets ID i
    // whatever order the units before it are in.
    SDL_GPUBuffer* m_ids = nullptr;
    std::uint32_t m_current = 0;

    // GpuUnitReorder's targets for the per-unit buffers above, which swap with
    // them every step; null unless reordering. (Units reorder into whichever
    // of m_units isn't current.)
    SDL_GPUBuffer* m_prevSpare = nullptr;
    SDL_GPUBuffer* m_healthSpare = nullptr;
    SDL_GPUBuffer* m_flingSpare = nullptr;
    SDL_GPUBuffer* m_idsSpare = nullptr;
    // Where each unit is by ID, rewritten after every reorder, so drawing
    // goes in ID order and overlaps don't flicker; null unless reordering.
    // Starts as identity, which holds until the first reorder.
    SDL_GPUBuffer* m_slots = nullptr;

    // Streaming spawns: units [m_unitCount, m_capacity) are still to come.
    std::uint32_t m_capacity = 0;
    SDL_GPUTransferBuffer* m_spawnTransfer = nullptr;
    SDL_GPUTransferBuffer* m_spawnHealthTransfer = nullptr;
    std::mt19937 m_spawnRng;
    double m_spawnDue = 0.0;
    std::uint32_t m_frame = 0;

    // Explosions still pushing, oldest first, with the share of each push
    // (1 to 0) still to come.
    struct ActiveExplosion {
        Explosion explosion;
        float left = 1.0f;
    };

    std::vector<ActiveExplosion> m_explosions;
    // Each unit's velocity from flinging explosions, on top of its (capped)
    // walking velocity. Zero for units that haven't been flung.
    SDL_GPUBuffer* m_fling = nullptr;
    float m_flingDrag = 3.0f;

    // Each unit's status effects (StatusEffect), and its reorder spare. Zero
    // for every unit, including those still to spawn, until something hits it.
    SDL_GPUBuffer* m_status = nullptr;
    SDL_GPUBuffer* m_statusSpare = nullptr;
    // What each source applies (TurretEffects::statusTable), uploaded when
    // the turrets are set.
    SDL_GPUBuffer* m_statusTable = nullptr;
    // This step's explosions and turret effects for unit_integrate (Effects in
    // unit_integrate.comp.hlsl), and the upload they go through each step.
    SDL_GPUBuffer* m_effects = nullptr;
    SDL_GPUTransferBuffer* m_effectsTransfer = nullptr;

    // The bullet pool, kMaxBullets of each: positions as BaseUnitData (so
    // m_renderer draws them), last step's positions, and per-bullet hits.
    // Shots take slots round in order, from m_nextBullet.
    SDL_GPUBuffer* m_bullets = nullptr;
    SDL_GPUBuffer* m_bulletPrev = nullptr;
    SDL_GPUBuffer* m_bulletParams = nullptr;
    std::uint32_t m_nextBullet = 0;
    // When each slot's bullet runs out of life at the latest, in simulated
    // seconds, so the panel can say how many could still be flying.
    std::vector<float> m_bulletExpiry;
    // Bullet damage per unit this step, in thousandths of a hit point. Never
    // reordered: bullet_step fills it after the reorder and unit_finalize
    // empties it before the next.
    SDL_GPUBuffer* m_pendingDamage = nullptr;
    SDL_GPUComputePipeline* m_firePipeline = nullptr;
    SDL_GPUComputePipeline* m_bulletPipeline = nullptr;

    TurretEffects m_turrets;
    // The level's own walls, without blockers; m_config.walls adds them.
    std::shared_ptr<const WallColliders> m_baseWalls;

    // Kills, leaks and what turrets need to hear back (kFeedbackWords uints,
    // laid out as TurretEffects.hpp says). Read back on a command buffer of
    // its own, one download in flight at a time, whose fence is polled rather
    // than waited on, so the main thread never stalls.
    SDL_GPUBuffer* m_feedback = nullptr;
    SDL_GPUTransferBuffer* m_feedbackTransfer = nullptr;
    SDL_GPUFence* m_feedbackFence = nullptr;
    UnitTally m_tally;
    SDL_GPUComputePipeline* m_aimPipeline = nullptr;

    SimConfig m_config;
    std::uint32_t m_unitCount = 0;
    float m_dt = 0.0f;
};

} // namespace horde::sim
