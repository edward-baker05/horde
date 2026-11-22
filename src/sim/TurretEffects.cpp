#include "sim/TurretEffects.hpp"

#include <algorithm>
#include <cmath>

namespace horde::sim {

namespace {

// How long a zapper's pulse lasts: long enough to be seen.
constexpr float kPulseDuration = 0.15f;
// Seconds from a mortar firing to its shell landing. Turret_aim's answer
// takes a few frames to come back, well inside this.
constexpr float kShellFlight = 0.8f;
// How soon a mortar that found nobody in range looks again.
constexpr float kRetryDelay = 0.5f;
constexpr float kBlastDuration = 0.2f;
// How much further than its range a bullet flies before it is spent.
constexpr float kBulletReach = 1.5f;
// Hit points one unit pressed against a blocker wears off it per second, so a
// crowd brings it down faster than a trickle.
constexpr float kCrushPerUnit = 1.0f;
// Feedback counts blocker damage in these, as whole numbers.
constexpr float kMilliHp = 1000.0f;

glm::vec4 effect(float range, float shapeParam, float damage, TurretShape shape, std::size_t slot) {
    const auto packed = static_cast<std::uint32_t>(shape) | static_cast<std::uint32_t>(slot) << kTurretShapeBits;
    return {range, shapeParam, damage, static_cast<float>(packed)};
}

// The status table entry for a turret slot.
std::uint32_t statusOf(std::size_t slot) {
    return static_cast<std::uint32_t>(slot + 1);
}

Explosion blast(glm::vec2 center, float radius, float damage, float fling, std::size_t slot) {
    Explosion e;
    e.center = center;
    e.radius = radius;
    e.strength = fling;
    e.fling = fling > 0.0f;
    e.duration = kBlastDuration;
    e.falloff = ExplosionFalloff::Linear;
    e.damage = damage;
    e.status = statusOf(slot);
    return e;
}

} // namespace

void TurretEffects::set(const std::vector<logic::Turret>& turrets) {
    m_turrets = turrets;
    if (m_turrets.size() > logic::kMaxTurrets) {
        m_turrets.resize(logic::kMaxTurrets);
    }
    // A full period first, so nothing fires the instant the wave starts.
    m_states.assign(m_turrets.size(), State{});
    for (std::size_t i = 0; i < m_turrets.size(); ++i) {
        m_states[i].cooldown = logic::turretDef(m_turrets[i].kind).period;
        m_states[i].hp = logic::turretDef(m_turrets[i].kind).maxHp;
    }
    m_time = 0.0f;
    m_blockersChanged = false;
}

bool TurretEffects::add(const std::vector<logic::Turret>& turrets) {
    if (m_turrets.size() + turrets.size() > logic::kMaxTurrets) {
        return false;
    }
    for (const logic::Turret& turret : turrets) {
        const logic::TurretDef& def = logic::turretDef(turret.kind);
        m_turrets.push_back(turret);
        State state;
        // A full period first, as at the start of the wave.
        state.cooldown = def.period;
        state.hp = def.maxHp;
        m_states.push_back(state);
    }
    return true;
}

std::vector<WallPrimitive> TurretEffects::blockerWalls() const {
    std::vector<WallPrimitive> out;
    for (std::size_t i = 0; i < m_turrets.size(); ++i) {
        const logic::Turret& turret = m_turrets[i];
        const logic::TurretDef& def = logic::turretDef(turret.kind);
        if (def.placement != logic::TurretClass::Blocker || m_states[i].phase == Phase::Spent) {
            continue;
        }
        out.push_back({turret.center,
                       {std::cos(turret.facing), std::sin(turret.facing)},
                       def.halfSize,
                       WallPrimitive::Box,
                       static_cast<float>(i + 1)});
    }
    return out;
}

void TurretEffects::packContacts(float dt, glm::vec4* out) const {
    for (std::size_t i = 0; i < logic::kMaxTurrets; ++i) {
        out[i] = glm::vec4(0.0f);
        if (i >= m_turrets.size() || m_states[i].phase == Phase::Spent) {
            continue;
        }
        const logic::TurretDef& def = logic::turretDef(m_turrets[i].kind);
        if (def.placement == logic::TurretClass::Blocker) {
            out[i] = glm::vec4(std::round(kCrushPerUnit * dt * kMilliHp), def.damage * dt, 0.0f, 0.0f);
        }
    }
}

std::vector<StatusEffect> TurretEffects::statusTable() const {
    std::vector<StatusEffect> table(kStatusTableSize);
    for (std::size_t i = 0; i < m_turrets.size(); ++i) {
        const logic::TurretDef& def = logic::turretDef(m_turrets[i].kind);
        StatusEffect& entry = table[statusOf(i)];
        entry.stun = def.stunSeconds;
        entry.slow = def.slowSeconds;
        entry.slowFactor = def.slowFactor;
        entry.burn = def.burnSeconds;
        entry.burnDps = def.burnDps;
    }
    return table;
}

float TurretEffects::aimOf(std::size_t index) const {
    const logic::Turret& turret = m_turrets[index];
    const logic::TurretDef& def = logic::turretDef(turret.kind);
    if (def.delivery != logic::TurretDelivery::Sweep || def.period <= 0.0f) {
        return turret.facing;
    }
    // Smoothly back and forth across the arc, once per period.
    return turret.facing + def.arc * 0.5f * std::sin(m_time * 6.2831853f / def.period);
}

void TurretEffects::readFeedback(const std::uint32_t* perTurret, float cellSize, glm::uvec2 gridDim) {
    for (std::size_t i = 0; i < m_turrets.size(); ++i) {
        const std::uint32_t* words = perTurret + i * kFeedbackPerTurret;
        State& state = m_states[i];
        const logic::TurretDef& def = logic::turretDef(m_turrets[i].kind);
        if (def.placement == logic::TurretClass::Blocker) {
            if (state.phase == Phase::Spent) {
                continue;
            }
            state.hp = std::max(def.maxHp - static_cast<float>(words[3]) / kMilliHp, 0.0f);
            if (state.hp <= 0.0f) {
                state.phase = Phase::Spent;
                m_blockersChanged = true;
            }
            continue;
        }
        switch (def.delivery) {
            case logic::TurretDelivery::Shell:
                // Only the answer to the shot still waiting; older ones are stale.
                if (state.phase != Phase::Aiming || words[0] != state.shotId) {
                    break;
                }
                if (words[2] == 0 || gridDim.x == 0) {
                    state.phase = Phase::Ready;
                    state.cooldown = kRetryDelay;
                    break;
                }
                state.target =
                    (glm::vec2(static_cast<float>(words[1] % gridDim.x), static_cast<float>(words[1] / gridDim.x)) +
                     0.5f) *
                    cellSize;
                state.landsIn = std::max(kShellFlight - state.aimedFor, 0.0f);
                state.phase = Phase::Flying;
                break;
            case logic::TurretDelivery::Mine:
                if (state.phase == Phase::Ready && words[3] > 0) {
                    state.phase = Phase::Triggered;
                }
                break;
            default:
                break;
        }
    }
}

std::uint32_t TurretEffects::step(float dt, glm::vec4* out, std::vector<Explosion>& blasts,
                                  std::vector<AimRequest>& aims, std::vector<FireRequest>& fires) {
    m_time += dt;
    std::uint32_t count = 0;
    for (std::size_t i = 0; i < m_turrets.size(); ++i) {
        const logic::Turret& turret = m_turrets[i];
        const logic::TurretDef& def = logic::turretDef(turret.kind);
        State& state = m_states[i];
        const float aim = aimOf(i);
        const glm::vec2 dir{std::cos(aim), std::sin(aim)};

        glm::vec4 shape{0.0f}; // set by every case that doesn't `continue`
        switch (def.delivery) {
            case logic::TurretDelivery::Cone:
                shape = effect(def.range, std::cos(def.arc * 0.5f), def.damage * dt, TurretShape::Cone, i);
                break;
            case logic::TurretDelivery::Sweep:
                shape = effect(def.range, def.blastRadius * 0.5f, def.damage * dt, TurretShape::Beam, i);
                break;
            case logic::TurretDelivery::Pulse: {
                state.cooldown -= dt;
                if (state.cooldown > 0.0f) {
                    continue;
                }
                state.cooldown += std::max(def.period, dt);
                // The same damage all the way out, and a push only if it has one.
                Explosion pulse;
                pulse.center = turret.center;
                pulse.radius = def.range;
                pulse.strength = def.blastPush;
                pulse.fling = def.blastPush > 0.0f;
                pulse.duration = kPulseDuration;
                pulse.falloff = ExplosionFalloff::Constant;
                pulse.damage = def.damage;
                pulse.status = statusOf(i);
                blasts.push_back(pulse);
                continue;
            }
            case logic::TurretDelivery::Shell:
                // Reloads while a shell is in the air, so shots stay a period apart.
                state.cooldown -= dt;
                if (state.phase == Phase::Ready) {
                    if (state.cooldown <= 0.0f) {
                        state.cooldown += std::max(def.period, dt);
                        state.phase = Phase::Aiming;
                        state.shotId = m_nextShotId++;
                        state.aimedFor = 0.0f;
                        aims.push_back({static_cast<std::uint32_t>(i), state.shotId, turret.center, def.range});
                    }
                } else if (state.phase == Phase::Aiming) {
                    state.aimedFor += dt;
                } else if (state.phase == Phase::Flying) {
                    state.landsIn -= dt;
                    if (state.landsIn <= 0.0f) {
                        blasts.push_back(blast(state.target, def.blastRadius, def.damage, def.blastPush, i));
                        state.phase = Phase::Ready;
                    }
                }
                continue;
            case logic::TurretDelivery::Mine:
                if (state.phase == Phase::Triggered) {
                    blasts.push_back(blast(turret.center, def.blastRadius, def.damage, def.blastPush, i));
                    state.phase = Phase::Spent;
                }
                if (state.phase != Phase::Ready) {
                    continue;
                }
                // Still armed: the GPU counts units that come near.
                shape = effect(def.range, 0.0f, 0.0f, TurretShape::Mine, i);
                break;
            case logic::TurretDelivery::Gun: {
                // At most one shot a step, however short the period.
                state.cooldown -= dt;
                if (state.cooldown > 0.0f || def.bulletSpeed <= 0.0f) {
                    continue;
                }
                state.cooldown = std::max(state.cooldown + def.period, 0.0f);
                FireRequest shot;
                shot.center = turret.center;
                shot.range = def.range;
                shot.speed = def.bulletSpeed;
                shot.damage = def.damage;
                shot.life = def.range * kBulletReach / def.bulletSpeed;
                shot.pierce = def.pierce;
                shot.status = statusOf(i);
                fires.push_back(shot);
                continue;
            }
            case logic::TurretDelivery::None:
                // Blockers collide rather than act.
                continue;
        }
        out[count * 2] = glm::vec4(turret.center, dir);
        out[count * 2 + 1] = shape;
        ++count;
    }
    return count;
}

} // namespace horde::sim
