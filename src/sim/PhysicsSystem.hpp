#pragma once

#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "logic/UnitData.hpp"

namespace horde::scene {
struct Services;
}

namespace horde::sim {

// Everything a physics system is handed when a level starts.
struct SimConfig {
    glm::vec2 worldSize{2000.0f, 2000.0f};
    float unitSize = 5.0f;
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

std::vector<BaseUnitData> spawnUnits(SpawnPattern pattern, std::uint32_t count, glm::vec2 worldSize, float unitSize,
                                     std::uint32_t seed);

} // namespace horde::sim
