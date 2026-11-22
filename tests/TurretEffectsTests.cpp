#include <doctest/doctest.h>

#include "logic/Turret.hpp"
#include "sim/TurretEffects.hpp"

using namespace horde;

namespace {

// One step of `dt`, returning the explosions it set off.
std::vector<sim::Explosion> step(sim::TurretEffects& effects, float dt) {
    std::vector<glm::vec4> out(logic::kMaxTurrets * 2);
    std::vector<sim::Explosion> blasts;
    std::vector<sim::AimRequest> aims;
    std::vector<sim::FireRequest> fires;
    effects.step(dt, out.data(), blasts, aims, fires);
    return blasts;
}

} // namespace

TEST_CASE("added turrets take the next slots and leave the rest's timers alone") {
    const float period = logic::turretDef(logic::TurretKind::Zapper).period;
    sim::TurretEffects effects;
    effects.set({logic::makeTurret(logic::TurretKind::Zapper, {100, 100}, 0.0f)});

    // Most of the way to the first pulse, then a second zapper arrives.
    CHECK(step(effects, period * 0.75f).empty());
    REQUIRE(effects.add({logic::makeTurret(logic::TurretKind::Zapper, {300, 300}, 0.0f)}));
    REQUIRE(effects.turrets().size() == 2);
    CHECK(effects.turrets()[1].center == glm::vec2(300, 300));

    // The first pulses on time; the newcomer waits a full period of its own.
    const std::vector<sim::Explosion> blasts = step(effects, period * 0.5f);
    REQUIRE(blasts.size() == 1);
    CHECK(blasts[0].center == glm::vec2(100, 100));
    CHECK(blasts[0].status == 1u);
}

TEST_CASE("adding past the turret cap refuses all of them") {
    sim::TurretEffects effects;
    effects.set(std::vector<logic::Turret>(logic::kMaxTurrets - 1,
                                           logic::makeTurret(logic::TurretKind::Barricade, {50, 50}, 0.0f)));
    const logic::Turret mine = logic::makeTurret(logic::TurretKind::Mine, {10, 10}, 0.0f);
    CHECK_FALSE(effects.add({mine, mine}));
    CHECK(effects.turrets().size() == logic::kMaxTurrets - 1);
    CHECK(effects.add({mine}));
    CHECK(effects.turrets().size() == logic::kMaxTurrets);
}

TEST_CASE("an added blocker stands as a wall tagged with its slot") {
    sim::TurretEffects effects;
    effects.set({logic::makeTurret(logic::TurretKind::Zapper, {100, 100}, 0.0f)});
    REQUIRE(effects.add({logic::makeTurret(logic::TurretKind::CrowdBarrier, {400, 400}, 0.0f)}));
    const std::vector<sim::WallPrimitive> walls = effects.blockerWalls();
    REQUIRE(walls.size() == 1);
    CHECK(walls[0].radius == doctest::Approx(2.0f)); // 1 + slot 1
    CHECK(effects.hp(1) == doctest::Approx(logic::turretDef(logic::TurretKind::CrowdBarrier).maxHp));
}
