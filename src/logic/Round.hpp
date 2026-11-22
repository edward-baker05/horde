#pragma once

#include <glm/vec2.hpp>

#include <cstdint>
#include <vector>

#include "logic/Level.hpp"
#include "sim/Explosion.hpp"

namespace horde::logic {

enum class RoundOutcome { Playing, Won, Lost };

// What happened to the crowd, cumulative since the level started. Every count
// only ever grows, so an older report is recognisable by its smaller numbers.
struct RoundCounters {
    std::uint32_t killed = 0;
    std::uint32_t leaked = 0;
};

// The player's click: a small free blast on a short cooldown, full damage
// across its radius so it kills what it lands on.
inline constexpr float kClickCooldown = 0.25f;
inline constexpr float kClickRadius = 12.0f;
inline constexpr float kClickDamage = 10.0f;

// One round's rules: money, kills, leaks and the outcome. Plain C++, with no
// SDL or GPU, so it runs in tests. The turrets themselves are the
// simulation's (sim::TurretEffects); the round only pays for them.
//
// A round opens in its prep phase: turrets can be bought, but the clock is
// stopped and nothing is reported. start() begins play. The scene feeds it the
// player's actions and the crowd's reports (applyCounters). Reports arrive a
// frame or so late and can arrive out of order, so every count it reads only
// ever moves forward. Once the round is won or lost, nothing changes it any
// more.
class Round {
public:
    explicit Round(const RoundSettings& settings);

    int money() const {
        return m_money;
    }

    RoundOutcome outcome() const {
        return m_outcome;
    }

    // Whether the prep phase is over.
    bool started() const {
        return m_started;
    }

    // Ends the prep phase: the clock runs and reports count from now on.
    void start();

    float seconds() const {
        return m_seconds;
    }

    std::uint32_t killed() const {
        return m_counters.killed;
    }

    std::uint32_t leaked() const {
        return m_counters.leaked;
    }

    const RoundSettings& settings() const {
        return m_settings;
    }

    // Units per second: faster the more of the horde is dead, so a quick
    // win comes from killing quickly all the way through.
    float spawnRate() const;

    // What winning pays toward upgrades; 0 unless the round was won.
    int upgradeMoney() const;

    // Pays `cost`, for turrets, in prep or in play. False, and nothing paid,
    // if there isn't enough money or the round is over.
    bool spend(int cost);

    void applyCounters(const RoundCounters& counters);

    // Advances the clock and the click's cooldown. Does nothing in prep or
    // once the round has ended.
    void update(float dt);

    // The player's free blast at `at`, if it has cooled down. Never in prep.
    bool click(glm::vec2 at);

    float clickCooldown() const {
        return m_clickCooldown;
    }

    // Blasts queued since the last call, oldest first.
    std::vector<sim::Explosion> takeBlasts();

private:
    bool playing() const {
        return m_started && m_outcome == RoundOutcome::Playing;
    }

    RoundSettings m_settings;
    int m_money = 0;
    float m_seconds = 0.0f;
    bool m_started = false;
    RoundOutcome m_outcome = RoundOutcome::Playing;
    // The furthest report seen, field by field.
    RoundCounters m_counters;
    float m_clickCooldown = 0.0f;
    std::vector<sim::Explosion> m_blasts;
};

} // namespace horde::logic
