#pragma once

#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "logic/UnitData.hpp"

namespace horde::scene {
struct Services;
}

namespace horde::sim {

class WallColliders;

// How a collision pass combines a unit's pair corrections. Both are Jacobi:
// within a pass every unit reads the same state and writes the next one, so
// the result doesn't depend on thread order.
enum class CollisionSolver {
    // Shift by the sum of the corrections times `relaxation`. Converges faster
    // in light contact; can overshoot in dense crowds if relaxation is high.
    Relaxed,
    // Shift by the average of the corrections (position-based dynamics'
    // constraint averaging) times `averageOmega`. At 1 it never overshoots
    // however many neighbours push at once, but converges slowly in deep
    // crowds; over-relaxing (omega 1-2) speeds that up. Ignores `relaxation`.
    Average,
};

bool parseSolver(const std::string& text, CollisionSolver& out);
const char* solverName(CollisionSolver solver);

// Unit-vs-unit collision rules. Shared by every system that resolves
// collisions, so that two systems are benchmarked against the same problem.
//
// Units are discs whose diameter is SimConfig::unitSize: two overlap when their
// positions are closer than that.
struct CollisionSettings {
    // Relaxation passes per frame. More converge further (less overlap left in
    // dense crowds) at a linear cost.
    std::uint32_t iterations = 4;
    CollisionSolver solver = CollisionSolver::Relaxed;
    // Relaxed solver: fraction of each pass's summed correction applied. Below
    // 1 because a unit pushed by several neighbours at once would otherwise
    // overshoot.
    float relaxation = 0.5f;
    // Average solver: over-relaxation factor on the mean correction. 1 is the
    // plain average; up to 2 converges faster (successive over-relaxation, as
    // in Mueller et al.'s PBD). Beyond 2 it diverges.
    float averageOmega = 1.0f;
    // Largest correction per pass, as a fraction of the diameter.
    float maxCorrection = 0.5f;
};

// Where units are trying to go, and how fast.
struct MovementSettings {
    glm::vec2 goal{0.0f, 0.0f};
    float maxSpeed = 60.0f; // world units per second
    float steering = 4.0f;  // per second: how quickly velocity turns toward the goal
};

// An axis-aligned area of the level: a spawn or exit marker's band.
struct MarkerArea {
    glm::vec2 center{0.0f, 0.0f};
    glm::vec2 halfSize{0.0f, 0.0f};
};

// How units enter and leave a level, for systems that support it
// (PhysicsSystem::supportsFlow).
struct FlowSettings {
    std::vector<MarkerArea> spawns;
    std::vector<MarkerArea> exits;
    // Units still to come in at the spawns after the start, spawnRate per
    // second; 0 brings them all in on the first frame.
    std::uint32_t toSpawn = 0;
    float spawnRate = 1000.0f;
    // A unit that reaches an exit re-enters at a spawn, keeping the flow
    // going. Otherwise it stops there.
    bool respawnAtExit = true;
    std::uint32_t seed = 1;
};

// Unit hit points. Units start at maxHp, are drawn from full colour at maxHp
// down to black at 0, and lose exitDamage each time they reach an exit.
// Health is a float so damage can be fractional; maxHp is a whole number.
struct HealthSettings {
    std::uint32_t maxHp = 30;
    float exitDamage = 1.0f;
};

// GPU flow shaders hold at most this many spawns and this many exits; any
// beyond are ignored, with a warning.
inline constexpr std::size_t kMaxFlowMarkers = 8;

// A level's field region, ready for simulation: a rotated box inside which a
// unit follows `direction` instead of heading for the exit, turning toward it
// at `strength` per second. Overlapping regions add as direction x strength.
struct FieldArea {
    glm::vec2 center{0.0f, 0.0f};
    glm::vec2 halfSize{0.0f, 0.0f};
    glm::vec2 direction{1.0f, 0.0f}; // unit length; also the box's local +x axis
    float strength = 0.0f;
};

// GPU shaders hold at most this many field regions; any beyond are ignored,
// with a warning.
inline constexpr std::size_t kMaxFieldRegions = 32;

// How an explosion fades from 1 at t = 0 toward 0 at t = 1: over its radius
// (t = distance / radius) and over its duration (t = time / duration).
enum class ExplosionFalloff {
    Constant,  // 1: the same everywhere, or all the way through
    Linear,    // 1 - t
    Quadratic, // (1 - t)^2: concentrated near the centre, or the start
};

const char* falloffName(ExplosionFalloff falloff);

// A debug blast: units within `radius` of `center` are pushed away from it,
// or pulled toward it when `strength` is negative. Further out they get less,
// as `falloff` says. The blast plays out over `duration` seconds (0: all in
// one step), front-loaded as `decay` says.
//
// Shoved (the default), a unit at the centre is moved `strength` world units
// in all and keeps walking at no more than the unit speed. Flung, it is
// launched at up to `strength` world units per second, past the unit speed,
// and slows down under the system's fling drag or on hitting something.
struct Explosion {
    glm::vec2 center{0.0f, 0.0f};
    float radius = 100.0f;
    float strength = 50.0f;
    float duration = 0.15f;
    ExplosionFalloff falloff = ExplosionFalloff::Linear;
    ExplosionFalloff decay = ExplosionFalloff::Constant;
    bool fling = false;
};

// GPU shaders hold at most this many explosions at once; a new one replaces
// the oldest.
inline constexpr std::size_t kMaxExplosions = 8;

// Everything a physics system is handed when a level starts.
struct SimConfig {
    glm::vec2 worldSize{2000.0f, 2000.0f};
    float unitSize = 5.0f;
    CollisionSettings collision;
    MovementSettings movement;
    // The level's walls, flattened and binned. Null or empty: no walls.
    std::shared_ptr<const WallColliders> walls;
    // Flows that steer units around walls. Only GPU systems apply them.
    std::vector<FieldArea> fieldRegions;
    FlowSettings flow;
    HealthSettings health;
    // Starting position and velocity of every unit. Its length is the unit
    // count; systems size their buffers from it.
    std::vector<BaseUnitData> initialUnits;
};

// One way of simulating (and drawing) the horde.
//
// Systems are swappable at runtime through the registry in PhysicsRegistry.cpp,
// which is what lets the benchmark run the same level against each of them.
// The hooks mirror Scene's. Physics runs at a fixed rate (LevelOptions::
// physicsRate), so a frame holds zero or more physics steps: LevelScene calls
// update(stepDt) once per step during the scene update, then compute() once
// per step on the frame's command buffer, then renderPass() once:
//
//   update()      CPU work for one step; must not rely on compute() running in between
//   compute()     GPU dispatches and uploads for one step, before the render pass begins
//   renderPass()  draws inside the frame's render pass, `alpha` of the way from
//                 the previous step's state to the latest
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

    virtual void renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const glm::mat4& viewProjection,
                            float alpha) = 0;

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

    // Copies every unit's hit points to the CPU, as readUnits does positions.
    // Returns false for systems that don't track health.
    virtual bool readHealth(std::vector<float>& out) {
        (void)out;
        return false;
    }

    // The collision settings actually in use. Null for systems that don't use
    // CollisionSettings.
    virtual const CollisionSettings* collisionSettings() const {
        return nullptr;
    }

    // Changes the collision passes per step while running, without a restart.
    // Returns false for systems that can't.
    virtual bool setCollisionPasses(std::uint32_t passes) {
        (void)passes;
        return false;
    }

    // Whether the system honours SimConfig::flow: streaming units in at the
    // spawns, steering them to the exits and respawning them there. Systems
    // that don't get every unit up front instead.
    virtual bool supportsFlow() const {
        return false;
    }

    // Whether addExplosion() does anything.
    virtual bool supportsExplosions() const {
        return false;
    }

    // Sets off an explosion from the next step. Returns false for systems
    // that can't.
    virtual bool addExplosion(const Explosion& explosion) {
        (void)explosion;
        return false;
    }

    // Explosions still pushing units, for drawing.
    virtual std::vector<Explosion> activeExplosions() const {
        return {};
    }

    // How quickly flung units slow down, per second: 3 loses about 95% of
    // the fling speed in one second.
    virtual void setFlingDrag(float perSecond) {
        (void)perSecond;
    }
};

// How starting units are laid out. All are deterministic for a given seed, so
// two benchmark runs with the same settings start from the same state.
enum class SpawnPattern {
    Column, // the original wrapping column from the middle of the level
    Random, // uniform over the level
    Grid,   // an even lattice over the level
    Markers, // at the level's spawn markers, streamed in over time where the system supports it
};

bool parseSpawnPattern(const std::string& text, SpawnPattern& out);
const char* spawnPatternName(SpawnPattern pattern);

// With `walls`, no unit starts inside one: random positions are re-drawn, and
// the regular patterns are pushed out to the nearest wall surface.
std::vector<BaseUnitData> spawnUnits(SpawnPattern pattern, std::uint32_t count, glm::vec2 worldSize, float unitSize,
                                     std::uint32_t seed, const WallColliders* walls = nullptr);

// `count` units at random points of random `areas` (positions are top-left
// corners, inside the level), avoiding walls where it can. For the Markers
// pattern and for streaming spawns in.
std::vector<BaseUnitData> spawnInAreas(std::uint32_t count, const std::vector<MarkerArea>& areas, glm::vec2 worldSize,
                                       float unitSize, std::mt19937& rng, const WallColliders* walls = nullptr);

} // namespace horde::sim
