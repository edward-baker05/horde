#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

#include "logic/Turret.hpp"
#include "sim/PhysicsSystem.hpp"
#include "sim/WallColliders.hpp"

namespace horde::sim {

// Status effects: a unit's own, or what a source applies. Matches UnitStatus
// in unit_status.hlsli.
struct StatusEffect {
    float stun = 0.0f; // seconds
    float slow = 0.0f; // seconds
    float slowFactor = 1.0f;
    float burn = 0.0f; // seconds
    float burnDps = 0.0f;
    float spare = 0.0f;
};
static_assert(sizeof(StatusEffect) == 24);

// Entries in the status table: 0 for nothing, then one per turret slot.
inline constexpr std::size_t kStatusTableSize = 1 + logic::kMaxTurrets;

// Bits a turret effect's shape takes in its packed last float; its turret
// slot sits above them. Matches TURRET_SHAPE_BITS in unit_integrate.comp.hlsl.
inline constexpr std::uint32_t kTurretShapeBits = 4;

// How a turret's effect is shaped this step. Matches TURRET_* in
// unit_integrate.comp.hlsl.
enum class TurretShape : std::uint32_t {
    Cone = 0, // damage within range, and within half the arc of its direction
    Beam = 1, // damage within half the width of the segment along its direction
    Mine = 2, // no damage: counts the units within range into the feedback buffer
};

// The feedback buffer the GPU writes and the CPU reads back without waiting:
// kFeedbackHeader uints (units killed, units leaked, two unused), then
// kFeedbackPerTurret for each turret slot:
//   [0] the ID of the latest shot turret_aim aimed for it
//   [1] that shot's target, as a grid cell index
//   [2] how many units were around that target
//   [3] a mine: units that came within its trigger radius; a blocker:
//       thousandths of a hit point units have worn off it. Running totals.
inline constexpr std::uint32_t kFeedbackHeader = 4;
inline constexpr std::uint32_t kFeedbackPerTurret = 4;
inline constexpr std::uint32_t kFeedbackWords =
    kFeedbackHeader + kFeedbackPerTurret * static_cast<std::uint32_t>(logic::kMaxTurrets);

// Bullets the GPU holds at once. Shots take the next slot round, so the oldest
// bullet is overwritten if the pool is ever full.
inline constexpr std::uint32_t kMaxBullets = 8192;

// A gun firing this step: turret_fire aims a bullet at the nearest unit in
// range and writes it into a slot of the bullet pool.
struct FireRequest {
    glm::vec2 center{0.0f, 0.0f};
    float range = 0.0f;
    float speed = 0.0f;  // world units per second
    float damage = 0.0f; // per hit
    float life = 0.0f;   // seconds before the bullet is spent anyway
    std::uint32_t pierce = 0;
    std::uint32_t status = 0; // status table index
};

// A mortar asking turret_aim for the thickest crowd in its range this step.
struct AimRequest {
    std::uint32_t slot = 0; // the mortar's turret index, which its feedback is under
    std::uint32_t shotId = 0;
    glm::vec2 center{0.0f, 0.0f};
    float range = 0.0f;
};

// The placed turrets during a wave, and their timers: which flamers and beams
// hurt units this step, which turrets go off as explosions, and which mortars
// need aiming.
//
// Each step, step() advances every timer and writes the flamers', beams' and
// mines' effects in the layout unit_integrate's Turrets array reads: two vec4s
// each, (centre, direction) then (range, cos half-angle or half-width,
// damage, shape | slot << kTurretShapeBits). Turrets that hit in bursts
// instead, such as a zapper's
// pulse, a mortar's shell or a mine going off, add an explosion, so they share
// its falloff and drawing.
//
// A mortar's shot goes ready -> aiming (an AimRequest this step; the GPU's
// answer comes back a few frames later through readFeedback) -> in flight ->
// landed, as an explosion at the target. A mine stays armed until feedback
// says a unit came near, then goes off and is spent. A blocker stands as a
// wall (blockerWalls) until feedback says it has been worn down to nothing,
// then it is spent and the walls need rebuilding.
class TurretEffects {
public:
    // Replaces every turret, with timers reset.
    void set(const std::vector<logic::Turret>& turrets);

    // What each source applies, kStatusTableSize entries: [0] nothing, then
    // each turret slot's effects from its definition.
    std::vector<StatusEffect> statusTable() const;

    // Advances `dt` and writes at most logic::kMaxTurrets effects to `out`,
    // returning how many it wrote. Appends any explosions due to `blasts` and
    // any mortars wanting a target to `aims` and any guns firing to `fires`.
    std::uint32_t step(float dt, glm::vec4* out, std::vector<Explosion>& blasts, std::vector<AimRequest>& aims,
                       std::vector<FireRequest>& fires);

    // Takes the per-turret part of a feedback download (logic::kMaxTurrets x
    // kFeedbackPerTurret uints) on the grid the GPU aimed with.
    void readFeedback(const std::uint32_t* perTurret, float cellSize, glm::uvec2 gridDim);

    const std::vector<logic::Turret>& turrets() const {
        return m_turrets;
    }

    // Where each beam points right now, for drawing; the facing for anything
    // else.
    float aimOf(std::size_t index) const;

    // Whether a mine has gone off or a blocker has been destroyed, so it is
    // no longer drawn.
    bool spent(std::size_t index) const {
        return m_states[index].phase == Phase::Spent;
    }

    // A blocker's hit points left; 0 for anything else.
    float hp(std::size_t index) const {
        return m_states[index].hp;
    }

    // The blockers still standing, as wall boxes tagged with their slots.
    std::vector<WallPrimitive> blockerWalls() const;

    // Whether a blocker has been destroyed since the last call, so the walls
    // need rebuilding without it.
    bool takeBlockersChanged() {
        const bool changed = m_blockersChanged;
        m_blockersChanged = false;
        return changed;
    }

    // Per turret slot, what one unit touching it does in a step of `dt`, for
    // unit_finalize: x = thousandths of a hit point the blocker loses, y = hit
    // points the unit loses. Zero for anything that isn't a standing blocker.
    void packContacts(float dt, glm::vec4* out) const;

private:
    enum class Phase : std::uint8_t {
        Ready,    // counting down to the next shot; an armed mine
        Aiming,   // a mortar waiting for turret_aim's answer
        Flying,   // a mortar's shell on its way to the target
        Triggered, // a mine feedback says a unit came near: goes off next step
        Spent,    // a mine that has gone off
    };

    struct State {
        Phase phase = Phase::Ready;
        float cooldown = 0.0f; // seconds until the next shot
        std::uint32_t shotId = 0;
        float aimedFor = 0.0f; // seconds since the shot was fired, while aiming
        float landsIn = 0.0f;  // seconds until the shell lands, while flying
        glm::vec2 target{0.0f, 0.0f};
        float hp = 0.0f; // blockers only
    };

    std::vector<logic::Turret> m_turrets;
    std::vector<State> m_states;
    std::uint32_t m_nextShotId = 1; // 0 is the feedback buffer's "never aimed"
    float m_time = 0.0f;
    bool m_blockersChanged = false;
};

} // namespace horde::sim
