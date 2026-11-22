#include "logic/Round.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace horde::logic {

Round::Round(const RoundSettings& settings, float stepSeconds)
    : m_settings(settings), m_stepSeconds(stepSeconds), m_money(settings.startMoney) {}

float Round::spawnRate() const {
    const float total = static_cast<float>(std::max<std::uint32_t>(m_settings.totalUnits, 1));
    const float progress = std::min(static_cast<float>(m_counters.killed), total) / total;
    return m_settings.baseSpawnRate * (1.0f + m_settings.spawnBoost * progress);
}

int Round::upgradeMoney() const {
    if (m_outcome != RoundOutcome::Won) {
        return 0;
    }
    const float bonus = std::max(0.0f, m_settings.parSeconds - m_seconds) * m_settings.bonusPerSecond;
    return m_settings.winReward + static_cast<int>(std::floor(bonus));
}

PlaceError Round::canPlace(TowerKind kind, glm::vec2 center, bool siteClear) const {
    if (!playing()) {
        return PlaceError::Ended;
    }
    if (!siteClear) {
        return PlaceError::Blocked;
    }
    bool free = false;
    for (const std::optional<Tower>& tower : m_towers) {
        if (!tower) {
            free = true;
        } else if (glm::distance(tower->center, center) < 2.0f * kTowerBodyRadius + kTowerGap) {
            return PlaceError::Blocked;
        }
    }
    if (!free) {
        return PlaceError::Full;
    }
    if (m_money < towerStats(kind, 1).cost) {
        return PlaceError::CannotAfford;
    }
    return PlaceError::None;
}

int Round::place(TowerKind kind, glm::vec2 center, bool siteClear) {
    if (canPlace(kind, center, siteClear) != PlaceError::None) {
        return -1;
    }
    for (std::size_t slot = 0; slot < m_towers.size(); ++slot) {
        if (m_towers[slot]) {
            continue;
        }
        const TowerStats stats = towerStats(kind, 1);
        Tower tower;
        tower.kind = kind;
        tower.center = center;
        tower.hp = stats.maxHp;
        // Contacts a previous tower in this slot ran up are not this one's.
        tower.contactBaseline = m_counters.contacts[slot];
        m_towers[slot] = tower;
        m_money -= stats.cost;
        return static_cast<int>(slot);
    }
    return -1;
}

int Round::towerAt(glm::vec2 point, float pickRadius) const {
    int best = -1;
    float bestDistance = pickRadius;
    for (std::size_t slot = 0; slot < m_towers.size(); ++slot) {
        if (!m_towers[slot]) {
            continue;
        }
        const float distance = glm::distance(m_towers[slot]->center, point);
        if (distance <= bestDistance) {
            best = static_cast<int>(slot);
            bestDistance = distance;
        }
    }
    return best;
}

bool Round::liveSlot(int slot) const {
    return slot >= 0 && slot < static_cast<int>(m_towers.size()) && m_towers[static_cast<std::size_t>(slot)];
}

Offer Round::offer(int slot) const {
    if (!liveSlot(slot)) {
        return {};
    }
    const Tower& tower = *m_towers[static_cast<std::size_t>(slot)];
    if (tower.level < kTowerLevels) {
        return {Offer::Kind::Upgrade, upgradeCost(tower.kind, tower.level + 1)};
    }
    return {Offer::Kind::Repair, repairCost(tower.kind)};
}

bool Round::buyOffer(int slot) {
    const Offer deal = offer(slot);
    if (!playing() || deal.kind == Offer::Kind::None || m_money < deal.cost) {
        return false;
    }
    Tower& tower = *m_towers[static_cast<std::size_t>(slot)];
    if (deal.kind == Offer::Kind::Upgrade) {
        ++tower.level;
    }
    // Both heal to full: that's the point of buying either.
    tower.hp = towerStats(tower.kind, tower.level).maxHp;
    m_money -= deal.cost;
    return true;
}

void Round::applyCounters(const sim::RoundCounters& counters) {
    if (!playing()) {
        return;
    }
    if (counters.killed > m_counters.killed) {
        m_money += static_cast<int>(counters.killed - m_counters.killed) * m_settings.killReward;
        m_counters.killed = counters.killed;
    }
    m_counters.leaked = std::max(m_counters.leaked, counters.leaked);

    for (std::size_t slot = 0; slot < m_towers.size(); ++slot) {
        m_counters.contacts[slot] = std::max(m_counters.contacts[slot], counters.contacts[slot]);
        std::optional<Tower>& tower = m_towers[slot];
        if (!tower || m_counters.contacts[slot] <= tower->contactBaseline) {
            continue;
        }
        const auto touches = static_cast<float>(m_counters.contacts[slot] - tower->contactBaseline);
        tower->contactBaseline = m_counters.contacts[slot];
        tower->hp -= touches * kContactDamagePerSecond * m_stepSeconds;
        if (tower->hp <= 0.0f) {
            tower.reset();
        }
    }

    // A loss is checked first: the last kill doesn't rescue a round that
    // leaked its limit in the same report.
    if (m_counters.leaked >= m_settings.leakLimit) {
        m_outcome = RoundOutcome::Lost;
    } else if (m_counters.killed + m_counters.leaked >= m_settings.totalUnits) {
        m_outcome = RoundOutcome::Won;
    }
}

void Round::update(float dt, const sim::DensityMap* density) {
    if (!playing()) {
        return;
    }
    m_seconds += dt;
    m_clickCooldown = std::max(0.0f, m_clickCooldown - dt);
    for (float& cooldown : m_abilityCooldowns) {
        cooldown = std::max(0.0f, cooldown - dt);
    }

    // Shots already in the air land before any new ones go up.
    for (Shot& shot : m_shots) {
        shot.age += dt;
        if (shot.age >= shot.flight) {
            queueBlast(shot.to, shot.blast);
        }
    }
    std::erase_if(m_shots, [](const Shot& shot) { return shot.age >= shot.flight; });

    for (std::size_t slot = 0; slot < m_towers.size(); ++slot) {
        std::optional<Tower>& tower = m_towers[slot];
        if (!tower) {
            continue;
        }
        tower->grow = std::min(1.0f, tower->grow + dt / kTowerGrowSeconds);
        tower->cooldown = std::max(0.0f, tower->cooldown - dt);
        const TowerStats stats = towerStats(tower->kind, tower->level);
        if (tower->grow < 1.0f || stats.firePeriod <= 0.0f || tower->cooldown > 0.0f || density == nullptr) {
            continue;
        }
        const std::optional<glm::vec2> target = aim(*tower, stats, *density);
        if (!target) {
            continue;
        }
        Shot shot;
        shot.slot = static_cast<int>(slot);
        shot.kind = tower->kind;
        shot.from = tower->center;
        shot.to = *target;
        shot.flight = stats.shotSpeed > 0.0f ? glm::distance(shot.from, shot.to) / stats.shotSpeed : 0.0f;
        shot.blast = stats.blast;
        if (shot.flight <= 0.0f) {
            queueBlast(shot.to, shot.blast);
        } else {
            m_shots.push_back(shot);
        }
        tower->cooldown = stats.firePeriod;
    }
}

std::optional<glm::vec2> Round::aim(const Tower& tower, const TowerStats& stats, const sim::DensityMap& density) const {
    // The map holds one unit per occupied cell, so this is the nearest of
    // those: within a cell's width of the true nearest. It aims at a real
    // unit rather than a cell's centre, so a small blast lands on someone.
    int best = -1;
    float bestDistance = stats.range;
    const std::size_t cells = std::min(density.counts.size(), density.samples.size());
    for (std::size_t i = 0; i < cells; ++i) {
        if (density.counts[i] == 0) {
            continue;
        }
        const float distance = glm::distance(density.samples[i], tower.center);
        if (distance <= bestDistance) {
            best = static_cast<int>(i);
            bestDistance = distance;
        }
    }
    if (best < 0) {
        return std::nullopt;
    }

    // Lead it: where it will be once the map's lag and the shot's flight have
    // passed. The flight depends on where that is, so one refinement.
    const auto cell = static_cast<std::size_t>(best);
    const glm::vec2 sample = density.samples[cell];
    if (cell >= density.velocities.size() || stats.shotSpeed <= 0.0f) {
        return sample;
    }
    const glm::vec2 velocity = density.velocities[cell];
    glm::vec2 target = sample + velocity * (density.age + bestDistance / stats.shotSpeed);
    target = sample + velocity * (density.age + glm::distance(target, tower.center) / stats.shotSpeed);
    return target;
}

bool Round::useAbility(AbilityKind kind, glm::vec2 at) {
    const AbilityStats stats = abilityStats(kind);
    float& cooldown = m_abilityCooldowns[static_cast<std::size_t>(kind)];
    if (!playing() || cooldown > 0.0f || m_money < stats.cost) {
        return false;
    }
    m_money -= stats.cost;
    cooldown = stats.cooldown;
    queueBlast(at, stats.blast);
    return true;
}

bool Round::click(glm::vec2 at) {
    if (!playing() || m_clickCooldown > 0.0f) {
        return false;
    }
    const AbilityStats stats = clickStats();
    m_clickCooldown = stats.cooldown;
    queueBlast(at, stats.blast);
    return true;
}

void Round::queueBlast(glm::vec2 at, const BlastStats& blast) {
    sim::Explosion explosion;
    explosion.center = at;
    explosion.radius = blast.radius;
    explosion.strength = blast.strength;
    explosion.duration = blast.duration;
    explosion.damage = blast.damage;
    explosion.falloff = blast.flat ? sim::ExplosionFalloff::Constant : sim::ExplosionFalloff::Linear;
    explosion.decay = sim::ExplosionFalloff::Constant;
    explosion.fling = false;
    m_blasts.push_back(explosion);
}

std::vector<sim::Explosion> Round::takeBlasts() {
    std::vector<sim::Explosion> out;
    out.swap(m_blasts);
    return out;
}

sim::CrowdEffects Round::effects() const {
    sim::CrowdEffects effects;
    effects.attractShare = kAttractShare;
    for (std::size_t slot = 0; slot < m_towers.size(); ++slot) {
        const std::optional<Tower>& tower = m_towers[slot];
        if (!tower) {
            continue;
        }
        effects.towers[slot] = {tower->center, kTowerBodyRadius * tower->grow};
        // Only a finished tower pulls or slows: while it grows in, the crowd
        // is still being eased out of its way.
        if (tower->grow < 1.0f) {
            continue;
        }
        effects.attractors.push_back({tower->center, kTowerPullRadius, kAttractStrength});
        const TowerStats stats = towerStats(tower->kind, tower->level);
        if (stats.speedFactor < 1.0f) {
            effects.slowZones.push_back({tower->center, stats.range, stats.speedFactor});
        }
    }
    return effects;
}

} // namespace horde::logic
