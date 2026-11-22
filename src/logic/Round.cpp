#include "logic/Round.hpp"

#include <algorithm>
#include <cmath>

namespace horde::logic {

Round::Round(const RoundSettings& settings) : m_settings(settings), m_money(settings.startMoney) {}

void Round::start() {
    m_started = true;
}

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

bool Round::spend(int cost) {
    if (m_outcome != RoundOutcome::Playing || cost < 0 || m_money < cost) {
        return false;
    }
    m_money -= cost;
    return true;
}

void Round::applyCounters(const RoundCounters& counters) {
    if (!playing()) {
        return;
    }
    if (counters.killed > m_counters.killed) {
        m_money += static_cast<int>(counters.killed - m_counters.killed) * m_settings.killReward;
        m_counters.killed = counters.killed;
    }
    m_counters.leaked = std::max(m_counters.leaked, counters.leaked);

    // A loss is checked first: the last kill doesn't rescue a round that
    // leaked its limit in the same report.
    if (m_counters.leaked >= m_settings.leakLimit) {
        m_outcome = RoundOutcome::Lost;
    } else if (m_counters.killed + m_counters.leaked >= m_settings.totalUnits) {
        m_outcome = RoundOutcome::Won;
    }
}

void Round::update(float dt) {
    if (!playing()) {
        return;
    }
    m_seconds += dt;
    m_clickCooldown = std::max(0.0f, m_clickCooldown - dt);
}

bool Round::click(glm::vec2 at) {
    if (!playing() || m_clickCooldown > 0.0f) {
        return false;
    }
    m_clickCooldown = kClickCooldown;
    sim::Explosion blast;
    blast.center = at;
    blast.radius = kClickRadius;
    blast.strength = 0.0f;
    blast.duration = 0.0f;
    blast.damage = kClickDamage;
    blast.falloff = sim::ExplosionFalloff::Constant;
    blast.decay = sim::ExplosionFalloff::Constant;
    blast.fling = false;
    m_blasts.push_back(blast);
    return true;
}

std::vector<sim::Explosion> Round::takeBlasts() {
    std::vector<sim::Explosion> out;
    out.swap(m_blasts);
    return out;
}

} // namespace horde::logic
