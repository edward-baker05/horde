#include <doctest/doctest.h>

#include "logic/Level.hpp"
#include "logic/TowerStats.hpp"

using namespace horde::logic;

TEST_CASE("tower stats at level 1 match the spec table") {
    CHECK(towerStats(TowerKind::Sniper, 1).cost == 150);
    CHECK(towerStats(TowerKind::Gunner, 1).range == doctest::Approx(250.0f));
    CHECK(towerStats(TowerKind::Pulse, 1).blast.strength == doctest::Approx(80.0f));
    CHECK(towerStats(TowerKind::Frost, 1).firePeriod == 0.0f);
    CHECK(towerStats(TowerKind::Frost, 1).speedFactor == doctest::Approx(0.4f));
    CHECK(towerStats(TowerKind::Sniper, 1).maxHp == doctest::Approx(200.0f));
}

TEST_CASE("shots fly at speeds that suit each tower") {
    const float sniper = towerStats(TowerKind::Sniper, 1).shotSpeed;
    const float gunner = towerStats(TowerKind::Gunner, 1).shotSpeed;
    const float pulse = towerStats(TowerKind::Pulse, 1).shotSpeed;
    CHECK(sniper > gunner);
    CHECK(gunner > pulse);
    CHECK(pulse > 0.0f);
    CHECK(towerStats(TowerKind::Frost, 1).shotSpeed == 0.0f);
}

TEST_CASE("a level-1 gunner kills a default unit in one shot") {
    CHECK(towerStats(TowerKind::Gunner, 1).blast.damage >= static_cast<float>(RoundSettings{}.unitHp));
}

TEST_CASE("upgrades raise each tower's main stat, range and health") {
    CHECK(towerStats(TowerKind::Sniper, 2).blast.damage == doctest::Approx(56.0f));
    CHECK(towerStats(TowerKind::Gunner, 3).firePeriod == doctest::Approx(0.1f / 1.96f));
    CHECK(towerStats(TowerKind::Frost, 2).speedFactor == doctest::Approx(0.3f));
    CHECK(towerStats(TowerKind::Frost, 3).speedFactor == doctest::Approx(0.2f));
    CHECK(towerStats(TowerKind::Pulse, 2).range == doctest::Approx(220.0f));
    CHECK(towerStats(TowerKind::Pulse, 2).blast.radius == doctest::Approx(168.0f));
    CHECK(towerStats(TowerKind::Pulse, 3).maxHp == doctest::Approx(450.0f));
    // Cost is what the tower cost to place, whatever its level.
    CHECK(towerStats(TowerKind::Sniper, 3).cost == 150);
}

TEST_CASE("upgrade and repair costs") {
    CHECK(upgradeCost(TowerKind::Gunner, 2) == 75);
    CHECK(upgradeCost(TowerKind::Gunner, 3) == 125);
    CHECK(repairCost(TowerKind::Pulse) == 50);
}

TEST_CASE("abilities and the click") {
    CHECK(abilityStats(AbilityKind::Bomb).cost == 50);
    CHECK(abilityStats(AbilityKind::Bomb).cooldown == doctest::Approx(8.0f));
    CHECK(abilityStats(AbilityKind::Gravity).blast.strength < 0.0f);
    CHECK(abilityStats(AbilityKind::Gravity).blast.damage == 0.0f);
    CHECK(clickStats().cost == 0);
    CHECK(clickStats().blast.radius == doctest::Approx(12.0f));
}
