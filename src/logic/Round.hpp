#pragma once

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "logic/Level.hpp"
#include "logic/TowerStats.hpp"
#include "sim/CrowdEffects.hpp"

namespace horde::logic {

enum class RoundOutcome { Playing, Won, Lost };
enum class PlaceError { None, CannotAfford, Full, Blocked, Ended };

struct Tower {
    TowerKind kind = TowerKind::Sniper;
    int level = 1;
    glm::vec2 center{0.0f};
    float hp = 0.0f;
    float grow = 0.0f;     // 0 to 1 over kTowerGrowSeconds; the body's radius scales with it
    float cooldown = 0.0f; // seconds until it may fire
    // The slot's contact count when this tower was last charged for contact.
    std::uint32_t contactBaseline = 0;
};

// A tower's shot on its way. Its blast goes off at `to` once it has flown
// for `flight` seconds; until then it is only something to draw.
struct Shot {
    int slot = -1; // the tower that fired it
    TowerKind kind = TowerKind::Sniper;
    glm::vec2 from{0.0f};
    glm::vec2 to{0.0f};
    float age = 0.0f;
    float flight = 0.0f;
    BlastStats blast;
};

// What the player can buy for a tower now: an upgrade below the top level,
// a repair at it.
struct Offer {
    enum class Kind { None, Upgrade, Repair };
    Kind kind = Kind::None;
    int cost = 0;
};

// One round's rules: money, towers, kills, leaks and the outcome. Plain C++,
// with no SDL or GPU, so it runs in tests.
//
// The scene feeds it the player's actions and the crowd's reports
// (applyCounters), and hands what it asks for back to the simulation each
// step. Reports arrive a frame or so late and can arrive out of order, so
// every count it reads only ever moves forward. Once the round is won or
// lost, nothing changes it any more.
class Round {
public:
    // `stepSeconds` is the length of one physics step, which is what one
    // reported contact is worth.
    Round(const RoundSettings& settings, float stepSeconds);

    int money() const {
        return m_money;
    }

    RoundOutcome outcome() const {
        return m_outcome;
    }

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

    const std::array<std::optional<Tower>, sim::kMaxTowers>& towers() const {
        return m_towers;
    }

    // Units per second: faster the more of the horde is dead, so a quick
    // win comes from killing quickly all the way through.
    float spawnRate() const;

    // What winning pays toward upgrades; 0 unless the round was won.
    int upgradeMoney() const;

    // `siteClear` is the caller's check of walls, markers and bounds
    // (sim::towerSiteClear); this adds money, room and other towers.
    PlaceError canPlace(TowerKind kind, glm::vec2 center, bool siteClear) const;
    // The slot it went into, or -1.
    int place(TowerKind kind, glm::vec2 center, bool siteClear);
    // The live tower whose centre is within `pickRadius` of `point`, or -1.
    int towerAt(glm::vec2 point, float pickRadius) const;
    Offer offer(int slot) const;
    bool buyOffer(int slot);

    void applyCounters(const sim::RoundCounters& counters);

    // Advances the clock, cooldowns, growth and shots in flight, and fires
    // every ready tower at the nearest unit in its range, led by how far it
    // will walk before the shot gets there. `density` may be null (no map
    // yet), and then no tower fires. Does nothing once the round has ended.
    void update(float dt, const sim::DensityMap* density);
    // Pays for an ability, starts its cooldown and queues its blast at `at`.
    bool useAbility(AbilityKind kind, glm::vec2 at);
    // The player's free blast at `at`, if it has cooled down.
    bool click(glm::vec2 at);

    float abilityCooldown(AbilityKind kind) const {
        return m_abilityCooldowns[static_cast<std::size_t>(kind)];
    }

    float clickCooldown() const {
        return m_clickCooldown;
    }

    // Blasts queued since the last call, oldest first.
    std::vector<sim::Explosion> takeBlasts();

    // Shots in flight, oldest first.
    const std::vector<Shot>& shots() const {
        return m_shots;
    }

    // What the towers ask of the crowd this step.
    sim::CrowdEffects effects() const;

private:
    bool playing() const {
        return m_outcome == RoundOutcome::Playing;
    }

    bool liveSlot(int slot) const;
    void queueBlast(glm::vec2 at, const BlastStats& blast);
    // Where `tower` should aim to hit the nearest unit the map knows of in
    // its range, or nothing.
    std::optional<glm::vec2> aim(const Tower& tower, const TowerStats& stats, const sim::DensityMap& density) const;

    RoundSettings m_settings;
    float m_stepSeconds = 0.0f;
    int m_money = 0;
    float m_seconds = 0.0f;
    RoundOutcome m_outcome = RoundOutcome::Playing;
    // The furthest report seen, field by field.
    sim::RoundCounters m_counters;
    std::array<std::optional<Tower>, sim::kMaxTowers> m_towers;
    std::array<float, kAbilityKindCount> m_abilityCooldowns{};
    float m_clickCooldown = 0.0f;
    std::vector<sim::Explosion> m_blasts;
    std::vector<Shot> m_shots;
};

} // namespace horde::logic
