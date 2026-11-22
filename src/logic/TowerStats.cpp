#include "logic/TowerStats.hpp"

#include <algorithm>
#include <cmath>

namespace horde::logic {

namespace {

// What each upgrade level multiplies by, compounding.
constexpr float kMainStatStep = 1.4f;
constexpr float kRangeStep = 1.1f;
constexpr float kHpStep = 1.5f;

TowerStats baseStats(TowerKind kind) {
    // Tuned so a level-1 Gunner kills a 10 HP unit a shot and pays for itself
    // in about ten seconds of steady fire; the first guesses left every level
    // short of the spawn rate.
    switch (kind) {
        case TowerKind::Sniper: // slow, hard-hitting, flies almost instantly
            return {150, 400.0f, 0.6f, {10.0f, 40.0f, 0.0f, 0.0f, true}, 3000.0f, 1.0f, 200.0f};
        case TowerKind::Gunner: // a stream of light rounds
            return {100, 250.0f, 0.1f, {6.0f, 10.0f, 0.0f, 0.0f, true}, 1400.0f, 1.0f, 200.0f};
        case TowerKind::Pulse: // a lobbed charge that bursts and shoves
            return {200, 200.0f, 2.0f, {120.0f, 6.0f, 80.0f, 0.25f, false}, 450.0f, 1.0f, 200.0f};
        case TowerKind::Frost:
            return {175, 180.0f, 0.0f, {}, 0.0f, 0.4f, 200.0f};
    }
    return {};
}

} // namespace

TowerStats towerStats(TowerKind kind, int level) {
    const int steps = std::clamp(level, 1, kTowerLevels) - 1;
    TowerStats stats = baseStats(kind);
    const float main = std::pow(kMainStatStep, static_cast<float>(steps));
    stats.range *= std::pow(kRangeStep, static_cast<float>(steps));
    stats.maxHp *= std::pow(kHpStep, static_cast<float>(steps));
    switch (kind) {
        case TowerKind::Sniper:
            stats.blast.damage *= main;
            break;
        case TowerKind::Gunner:
            stats.firePeriod /= main;
            break;
        case TowerKind::Pulse:
            stats.blast.strength *= main;
            stats.blast.radius *= main;
            break;
        case TowerKind::Frost:
            // Slower is stronger, so this one steps down rather than multiplying.
            stats.speedFactor = 0.4f - 0.1f * static_cast<float>(steps);
            break;
    }
    return stats;
}

int upgradeCost(TowerKind kind, int toLevel) {
    const float base = static_cast<float>(baseStats(kind).cost);
    return static_cast<int>(std::lround(base * (toLevel >= 3 ? 1.25f : 0.75f)));
}

int repairCost(TowerKind kind) {
    return static_cast<int>(std::lround(static_cast<float>(baseStats(kind).cost) * 0.25f));
}

AbilityStats abilityStats(AbilityKind kind) {
    switch (kind) {
        case AbilityKind::Bomb:
            return {50, 8.0f, {80.0f, 20.0f, 20.0f, 0.0f, false}};
        case AbilityKind::Gravity:
            // Your gravity bomb: a pull held for a few seconds, no damage.
            return {75, 15.0f, {200.0f, 0.0f, -150.0f, 3.0f, false}};
    }
    return {};
}

AbilityStats clickStats() {
    return {0, 0.25f, {12.0f, 10.0f, 0.0f, 0.0f, true}};
}

const char* towerName(TowerKind kind) {
    switch (kind) {
        case TowerKind::Sniper:
            return "Sniper";
        case TowerKind::Gunner:
            return "Gunner";
        case TowerKind::Pulse:
            return "Pulse";
        case TowerKind::Frost:
            return "Frost";
    }
    return "";
}

const char* abilityName(AbilityKind kind) {
    return kind == AbilityKind::Bomb ? "Bomb" : "Gravity";
}

} // namespace horde::logic
