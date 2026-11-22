#pragma once

#include <cstdint>

// Every tower, ability and click number in one place, so balancing a round is
// an edit here and never a shader change. The values are first guesses from
// docs/superpowers/specs/2026-11-21-round-play-design.md, to be tuned in play.

namespace horde::logic {

enum class TowerKind : std::uint8_t { Sniper, Gunner, Pulse, Frost };
enum class AbilityKind : std::uint8_t { Bomb, Gravity };

inline constexpr int kTowerKindCount = 4;
inline constexpr int kAbilityKindCount = 2;
inline constexpr int kTowerLevels = 3;

// World units. Units are 5 across, so a tower is about eight of them wide.
inline constexpr float kTowerBodyRadius = 20.0f;
// Space kept between two towers: one unit's width, so a unit that wanders
// between them can always get out rather than being stuck inside one.
inline constexpr float kTowerGap = 5.0f;
// From the tower's centre: how far its pull on the crowd reaches.
inline constexpr float kTowerPullRadius = 150.0f;
// A new tower's body grows from nothing to full size over this long, so it
// eases units aside rather than shoving them.
inline constexpr float kTowerGrowSeconds = 1.0f;
// Hit points a tower loses per second for each unit touching it.
inline constexpr float kContactDamagePerSecond = 0.3f;
// The share of the crowd that responds to a tower's pull at all.
inline constexpr float kAttractShare = 0.6f;
// How quickly attracted units turn toward a tower, per second.
inline constexpr float kAttractStrength = 3.0f;

// A blast as the simulation takes it, less where it goes off. Strength is a
// shove in world units; negative pulls.
struct BlastStats {
    float radius = 0.0f;
    float damage = 0.0f;
    float strength = 0.0f;
    float duration = 0.0f;
    // Full damage across the whole radius instead of fading to the edge. For
    // small aimed blasts, which would otherwise only kill dead centre.
    bool flat = false;
};

struct TowerStats {
    int cost = 0; // to place; upgrades are priced by upgradeCost()
    float range = 0.0f;
    float firePeriod = 0.0f; // seconds between shots; 0 never fires
    BlastStats blast;
    // How fast a shot flies to its target, world units per second; its blast
    // goes off when it gets there. 0 lands at once.
    float shotSpeed = 0.0f;
    float speedFactor = 1.0f; // Frost's slow zone, within `range`; 1 for the rest
    float maxHp = 200.0f;
};

// `level` is 1 to kTowerLevels.
TowerStats towerStats(TowerKind kind, int level);
// Cost to upgrade to `toLevel`, 2 or 3.
int upgradeCost(TowerKind kind, int toLevel);
// Cost to heal a fully upgraded tower back to full.
int repairCost(TowerKind kind);

struct AbilityStats {
    int cost = 0;
    float cooldown = 0.0f;
    BlastStats blast;
};

AbilityStats abilityStats(AbilityKind kind);
// The player's click: free, always available, on a short cooldown.
AbilityStats clickStats();

const char* towerName(TowerKind kind);
const char* abilityName(AbilityKind kind);

} // namespace horde::logic
