#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// What a round asks of the crowd each step, and what the crowd reports back.
// Plain data with no SDL or GPU types, so the round rules (logic::Round) can
// produce and consume it in headless tests.

namespace horde::sim {

// GPU shaders hold at most this many towers: colliders, attractors and slow
// zones alike. Matches MAX_TOWERS in shaders/src/walls.hlsli.
inline constexpr std::size_t kMaxTowers = 32;

// GPU shaders hold at most this many explosions at once; a new one replaces
// the oldest. Enough for every tower, the click and an ability to fire in
// the same step.
inline constexpr std::size_t kMaxExplosions = 32;

// How an explosion fades from 1 at t = 0 toward 0 at t = 1: over its radius
// (t = distance / radius) and over its duration (t = time / duration).
enum class ExplosionFalloff {
    Constant,  // 1: the same everywhere, or all the way through
    Linear,    // 1 - t
    Quadratic, // (1 - t)^2: concentrated near the centre, or the start
};

// A blast: units within `radius` of `center` are pushed away from it, or
// pulled toward it when `strength` is negative. Further out they get less,
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
    // Hit points a unit at the centre loses over the whole blast, fading
    // toward the edge by `falloff` like the push. A unit left with none is
    // despawned.
    float damage = 0.0f;
};

// A tower's body: a solid disc units are pushed out of, like a wall. Radius 0
// marks an empty slot.
struct TowerCollider {
    glm::vec2 center{0.0f};
    float radius = 0.0f;
};

// A pull on the crowd: inside `radius`, units that respond steer toward
// `center`, turning at `strength` per second. They walk there; nothing moves
// them bodily.
struct Attractor {
    glm::vec2 center{0.0f};
    float radius = 0.0f;
    float strength = 0.0f;
};

// Units inside `radius` of `center` walk at `speedFactor` of their speed.
struct SlowZone {
    glm::vec2 center{0.0f};
    float radius = 0.0f;
    float speedFactor = 1.0f;
};

// Everything a round adds to the crowd's step.
struct CrowdEffects {
    // By tower slot, because contacts are reported per slot.
    std::array<TowerCollider, kMaxTowers> towers{};
    std::vector<Attractor> attractors; // at most kMaxTowers
    std::vector<SlowZone> slowZones;   // at most kMaxTowers
    // The share of units, picked by a hash of their ID, that respond to
    // attractors at all: the rest carry on for the exit.
    float attractShare = 0.0f;
};

// What happened to the crowd, cumulative since the level started. Every count
// only ever grows, so an older report is recognisable by its smaller numbers.
struct RoundCounters {
    std::uint32_t killed = 0;
    std::uint32_t leaked = 0;
    // Steps x units touching each tower slot.
    std::array<std::uint32_t, kMaxTowers> contacts{};
};

// Where the crowd is, coarsely: units per cell, and one unit's centre and
// walking velocity in each.
struct DensityMap {
    glm::ivec2 dim{0, 0};
    float cellSize = 32.0f;
    std::vector<std::uint32_t> counts; // dim.x * dim.y, row-major
    std::vector<glm::vec2> samples;    // meaningless where the count is 0
    std::vector<glm::vec2> velocities; // world units per second, by cell; may be empty (standing still)
    // Seconds since the samples were where they say, so an aim can lead them.
    float age = 0.0f;
};

} // namespace horde::sim
