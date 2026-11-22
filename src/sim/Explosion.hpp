#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>

// Blasts as the simulation takes them. Plain data with no SDL or GPU types, so
// the round rules (logic::Round) can make them in headless tests.

namespace horde::sim {

// GPU shaders hold at most this many explosions at once; a new one replaces
// the oldest. High enough that turrets firing together should never reach it;
// each unit only loops over the active ones, so the headroom costs nothing.
// Matches MAX_EXPLOSIONS in unit_integrate.comp.hlsl.
inline constexpr std::size_t kMaxExplosions = 256;

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
    // Status effects it sets on every unit it reaches: an index into the
    // status table (TurretEffects::statusTable), 1 + a turret's slot. 0: none.
    std::uint32_t status = 0;
};

} // namespace horde::sim
