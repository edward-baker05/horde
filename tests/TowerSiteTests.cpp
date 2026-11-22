#include <doctest/doctest.h>

#include "logic/Level.hpp"
#include "logic/TowerStats.hpp"
#include "sim/TowerSite.hpp"
#include "sim/WallColliders.hpp"

using namespace horde;

TEST_CASE("tower sites keep clear of walls, markers and the level edge") {
    logic::Level level = logic::makeDefaultLevel(); // 2000 square, spawn all along west, exit all along east
    logic::Wall wall;
    wall.center = {1000.0f, 1000.0f};
    wall.shape = logic::RectangleShape{{50.0f, 50.0f}};
    level.walls.push_back(wall);

    constexpr float unit = 5.0f;
    sim::WallColliders walls;
    walls.build(level, logic::kTowerBodyRadius + unit);

    CHECK(sim::towerSiteClear(level, walls, {600, 600}, unit));
    CHECK_FALSE(sim::towerSiteClear(level, walls, {1000, 1000}, unit)); // in the wall
    CHECK_FALSE(sim::towerSiteClear(level, walls, {1000, 1070}, unit)); // 20 from its face: under 25
    CHECK(sim::towerSiteClear(level, walls, {1000, 1080}, unit));       // 30 clear
    CHECK_FALSE(sim::towerSiteClear(level, walls, {30, 600}, unit));    // overlaps the spawn band
    CHECK(sim::towerSiteClear(level, walls, {45, 600}, unit));          // clear of it
    CHECK_FALSE(sim::towerSiteClear(level, walls, {1970, 600}, unit));  // overlaps the exit band
    CHECK_FALSE(sim::towerSiteClear(level, walls, {600, 10}, unit));    // partly outside the level
    CHECK_FALSE(sim::towerSiteClear(level, walls, {600, 22}, unit));    // inside, but pinning units on the edge
    CHECK(sim::towerSiteClear(level, walls, {600, 25}, unit));          // a unit's width clear of it
}
