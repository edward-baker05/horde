#pragma once

#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "logic/UnitData.hpp"

namespace horde::scene {
struct Services;
}

namespace horde::sim {

class WallColliders;

// How a GPU system finds each unit's neighbours. See GpuSpatialGrid.
enum class Broadphase {
    BitonicSort,  // sort (cell, unit) keys: O(n log^2 n), ~200 passes at 1M units
    CountingSort, // count, prefix-sum, scatter: O(n + cells), a fixed handful of passes
    Buckets,      // fixed-size bucket per cell, filled with atomics: one pass, can drop neighbours when full
};

bool parseBroadphase(const std::string& text, Broadphase& out);
const char* broadphaseName(Broadphase broadphase);

// Unit-vs-unit collision rules. Shared by every system that resolves
// collisions, so that two systems are benchmarked against the same problem.
//
// Units are discs whose diameter is SimConfig::unitSize: two overlap when their
// positions are closer than that.
struct CollisionSettings {
    // Relaxation passes per frame. More converge further (less overlap left in
    // dense crowds) at a linear cost.
    std::uint32_t iterations = 4;
    // Fraction of each pass's summed correction applied. Below 1 because a unit
    // pushed by several neighbours at once would otherwise overshoot.
    float relaxation = 0.5f;
    // Largest correction per pass, as a fraction of the diameter.
    float maxCorrection = 0.5f;
    Broadphase broadphase = Broadphase::CountingSort;
    // Buckets only: units a cell can hold before neighbours are missed. Four
    // non-overlapping units fill a one-diameter cell; the rest is squeeze room.
    std::uint32_t bucketCapacity = 8;
};

// Collision settings scaled to a performance budget, from integrated graphics
// ("low") to a high-end card ("ultra"). Returns false for an unknown name.
bool collisionPreset(const std::string& name, CollisionSettings& out);

// Where units are trying to go, and how fast.
struct MovementSettings {
    glm::vec2 goal{0.0f, 0.0f};
    float maxSpeed = 60.0f; // world units per second
    float steering = 4.0f;  // per second: how quickly velocity turns toward the goal
};

// Everything a physics system is handed when a level starts.
struct SimConfig {
    glm::vec2 worldSize{2000.0f, 2000.0f};
    float unitSize = 5.0f;
    CollisionSettings collision;
    MovementSettings movement;
    // The level's walls, flattened and binned. Null or empty: no walls.
    std::shared_ptr<const WallColliders> walls;
    // Starting position and velocity of every unit. Its length is the unit
    // count; systems size their buffers from it.
    std::vector<BaseUnitData> initialUnits;
};

// One way of simulating (and drawing) the horde.
//
// Systems are swappable at runtime through the registry in PhysicsRegistry.cpp,
// which is what lets the benchmark run the same level against each of them.
// The hooks mirror Scene's, and LevelScene forwards to them in the same order:
//
//   update()      CPU work for this frame
//   compute()     GPU dispatches and uploads, before the render pass begins
//   renderPass()  draws inside the frame's render pass
class PhysicsSystem {
public:
    virtual ~PhysicsSystem() = default;

    // GPU and CPU setup. Return false to abort entering the level.
    virtual bool init(scene::Services& services, const SimConfig& config) = 0;

    // Release GPU resources. Called before the device goes away.
    virtual void shutdown() {}

    virtual void update(float dt) {
        (void)dt;
    }

    virtual void compute(SDL_GPUCommandBuffer* commands) {
        (void)commands;
    }

    virtual void renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                            const glm::mat4& viewProjection) = 0;

    // Extra lines for the level's debug panel.
    virtual void debugUi() {}

    virtual std::uint32_t unitCount() const = 0;

    // Copies the current units to the CPU. Slow (GPU systems stall for a
    // download), so for tests and benchmark measurements, not every frame.
    // Returns false if the system cannot.
    virtual bool readUnits(std::vector<BaseUnitData>& out) {
        (void)out;
        return false;
    }

    // The collision settings actually in use, which can differ from what the
    // system was handed (a registry entry may pin the broadphase). Null for
    // systems that don't use CollisionSettings.
    virtual const CollisionSettings* collisionSettings() const {
        return nullptr;
    }
};

// How starting units are laid out. All are deterministic for a given seed, so
// two benchmark runs with the same settings start from the same state.
enum class SpawnPattern {
    Column, // the original wrapping column from the middle of the level
    Random, // uniform over the level
    Grid,   // an even lattice over the level
};

bool parseSpawnPattern(const std::string& text, SpawnPattern& out);
const char* spawnPatternName(SpawnPattern pattern);

// With `walls`, no unit starts inside one: random positions are re-drawn, and
// the regular patterns are pushed out to the nearest wall surface.
std::vector<BaseUnitData> spawnUnits(SpawnPattern pattern, std::uint32_t count, glm::vec2 worldSize, float unitSize,
                                     std::uint32_t seed, const WallColliders* walls = nullptr);

} // namespace horde::sim
