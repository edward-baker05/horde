#include <doctest/doctest.h>

#include "logic/Round.hpp"

using namespace horde::logic;
using horde::sim::RoundCounters;

namespace {

constexpr float kStep = 1.0f / 60.0f;

RoundSettings small() {
    RoundSettings settings;
    settings.totalUnits = 100;
    settings.leakLimit = 10;
    settings.startMoney = 400;
    settings.killReward = 2;
    return settings;
}

RoundCounters counters(std::uint32_t killed, std::uint32_t leaked = 0) {
    RoundCounters c;
    c.killed = killed;
    c.leaked = leaked;
    return c;
}

} // namespace

TEST_CASE("placing spends money and fills a slot") {
    Round round(small(), kStep);
    CHECK(round.place(TowerKind::Gunner, {500, 500}, true) == 0);
    CHECK(round.money() == 300);
    REQUIRE(round.towers()[0].has_value());
    CHECK(round.towers()[0]->hp == doctest::Approx(200.0f));
    CHECK(round.towers()[0]->level == 1);
    CHECK(round.towers()[0]->grow == 0.0f);
}

TEST_CASE("cannot afford") {
    RoundSettings settings = small();
    settings.startMoney = 100;
    Round round(settings, kStep);
    CHECK(round.canPlace(TowerKind::Sniper, {500, 500}, true) == PlaceError::CannotAfford);
    CHECK(round.place(TowerKind::Sniper, {500, 500}, true) == -1);
    CHECK(round.money() == 100);
}

TEST_CASE("blocked by site or another tower") {
    Round round(small(), kStep);
    CHECK(round.canPlace(TowerKind::Gunner, {500, 500}, false) == PlaceError::Blocked);
    REQUIRE(round.place(TowerKind::Gunner, {500, 500}, true) == 0);
    CHECK(round.canPlace(TowerKind::Gunner, {530, 500}, true) == PlaceError::Blocked);
    CHECK(round.canPlace(TowerKind::Gunner, {546, 500}, true) == PlaceError::None);
}

TEST_CASE("full at 32 towers") {
    RoundSettings settings = small();
    settings.startMoney = 100000;
    Round round(settings, kStep);
    for (int i = 0; i < 32; ++i) {
        REQUIRE(round.place(TowerKind::Gunner, {50.0f + 50.0f * static_cast<float>(i), 500.0f}, true) == i);
    }
    CHECK(round.canPlace(TowerKind::Gunner, {500, 900}, true) == PlaceError::Full);
}

TEST_CASE("upgrade heals and raises level; repair at max") {
    RoundSettings settings = small();
    settings.startMoney = 1000;
    Round round(settings, kStep);
    REQUIRE(round.place(TowerKind::Gunner, {500, 500}, true) == 0);
    RoundCounters c;
    c.contacts[0] = 6000; // 6000 * 0.3 / 60 = 30 hp
    round.applyCounters(c);
    CHECK(round.towers()[0]->hp == doctest::Approx(170.0f));

    Offer offer = round.offer(0);
    CHECK(offer.kind == Offer::Kind::Upgrade);
    CHECK(offer.cost == 75);
    CHECK(round.buyOffer(0));
    CHECK(round.towers()[0]->level == 2);
    CHECK(round.towers()[0]->hp == doctest::Approx(towerStats(TowerKind::Gunner, 2).maxHp));
    CHECK(round.money() == 1000 - 100 - 75);

    CHECK(round.buyOffer(0));
    CHECK(round.towers()[0]->level == 3);
    CHECK(round.money() == 1000 - 100 - 75 - 125);

    c.contacts[0] = 12000;
    round.applyCounters(c);
    offer = round.offer(0);
    CHECK(offer.kind == Offer::Kind::Repair);
    CHECK(offer.cost == repairCost(TowerKind::Gunner));
    const int before = round.money();
    CHECK(round.buyOffer(0));
    CHECK(round.towers()[0]->level == 3);
    CHECK(round.towers()[0]->hp == doctest::Approx(towerStats(TowerKind::Gunner, 3).maxHp));
    CHECK(round.money() == before - repairCost(TowerKind::Gunner));
}

TEST_CASE("an offer the player cannot afford is refused") {
    RoundSettings settings = small();
    settings.startMoney = 150;
    Round round(settings, kStep);
    REQUIRE(round.place(TowerKind::Gunner, {500, 500}, true) == 0);
    CHECK_FALSE(round.buyOffer(0));
    CHECK(round.towers()[0]->level == 1);
    CHECK(round.money() == 50);
}

TEST_CASE("kills pay and stale reports are ignored") {
    Round round(small(), kStep);
    round.applyCounters(counters(5));
    CHECK(round.money() == 410);
    CHECK(round.killed() == 5);
    round.applyCounters(counters(3));
    CHECK(round.money() == 410);
    CHECK(round.killed() == 5);
}

TEST_CASE("contact damage wears a tower down and destroys it") {
    Round round(small(), kStep);
    REQUIRE(round.place(TowerKind::Gunner, {500, 500}, true) == 0);
    RoundCounters c;
    c.contacts[0] = 600;
    round.applyCounters(c);
    CHECK(round.towers()[0]->hp == doctest::Approx(197.0f));
    c.contacts[0] = 40600;
    round.applyCounters(c);
    CHECK_FALSE(round.towers()[0].has_value());
    CHECK(round.offer(0).kind == Offer::Kind::None);
    CHECK_FALSE(round.buyOffer(0));
    CHECK(round.towerAt({500, 500}, 25.0f) == -1);
}

TEST_CASE("an older contact report does not damage a tower twice") {
    Round round(small(), kStep);
    REQUIRE(round.place(TowerKind::Gunner, {500, 500}, true) == 0);
    RoundCounters c;
    c.contacts[0] = 600;
    round.applyCounters(c);
    c.contacts[0] = 300;
    round.applyCounters(c);
    c.contacts[0] = 600;
    round.applyCounters(c);
    CHECK(round.towers()[0]->hp == doctest::Approx(197.0f));
}

TEST_CASE("a tower placed into a used slot starts from that slot's count") {
    RoundSettings settings = small();
    settings.startMoney = 1000;
    Round round(settings, kStep);
    REQUIRE(round.place(TowerKind::Gunner, {500, 500}, true) == 0);
    RoundCounters c;
    c.contacts[0] = 1000000; // far past dead
    round.applyCounters(c);
    REQUIRE_FALSE(round.towers()[0].has_value());
    REQUIRE(round.place(TowerKind::Gunner, {500, 500}, true) == 0);
    round.applyCounters(c);
    CHECK(round.towers()[0]->hp == doctest::Approx(200.0f));
}

TEST_CASE("towerAt finds a tower within the pick radius") {
    Round round(small(), kStep);
    REQUIRE(round.place(TowerKind::Gunner, {500, 500}, true) == 0);
    CHECK(round.towerAt({510, 510}, 20.0f) == 0);
    CHECK(round.towerAt({530, 500}, 20.0f) == -1);
}

TEST_CASE("leak limit loses, even on the last kill") {
    Round round(small(), kStep);
    round.applyCounters(counters(90, 10));
    CHECK(round.outcome() == RoundOutcome::Lost);
    CHECK(round.upgradeMoney() == 0);
}

TEST_CASE("all dealt with under the limit wins") {
    Round round(small(), kStep);
    round.applyCounters(counters(94, 5));
    CHECK(round.outcome() == RoundOutcome::Playing);
    round.applyCounters(counters(95, 5));
    CHECK(round.outcome() == RoundOutcome::Won);
}

TEST_CASE("ended rounds ignore actions") {
    Round round(small(), kStep);
    REQUIRE(round.place(TowerKind::Gunner, {500, 500}, true) == 0);
    round.applyCounters(counters(95, 5));
    REQUIRE(round.outcome() == RoundOutcome::Won);
    const int money = round.money();
    CHECK(round.canPlace(TowerKind::Gunner, {900, 900}, true) == PlaceError::Ended);
    CHECK(round.place(TowerKind::Gunner, {900, 900}, true) == -1);
    CHECK_FALSE(round.buyOffer(0));
    round.applyCounters(counters(100, 9));
    CHECK(round.money() == money);
    CHECK(round.outcome() == RoundOutcome::Won);
}

TEST_CASE("spawn rate rises with kills") {
    Round round(small(), kStep); // base 30, boost 4, total 100
    CHECK(round.spawnRate() == doctest::Approx(30.0f));
    round.applyCounters(counters(50));
    CHECK(round.spawnRate() == doctest::Approx(90.0f));
    round.applyCounters(counters(100));
    CHECK(round.spawnRate() == doctest::Approx(150.0f));
}

namespace {

// A grown tower of `kind` at {500,500}, with plenty of money.
Round roundWithGrown(TowerKind kind) {
    RoundSettings settings = small();
    settings.startMoney = 10000;
    Round round(settings, kStep);
    REQUIRE(round.place(kind, {500, 500}, true) == 0);
    round.update(kTowerGrowSeconds + 0.01f, nullptr);
    return round;
}

// 10x10 cells of 32: a few units 160 right of a tower at {500,500}, a big
// crowd 320 left of it.
horde::sim::DensityMap crowdMap(glm::vec2 near, glm::vec2 far) {
    horde::sim::DensityMap map;
    map.dim = {40, 40};
    map.cellSize = 32.0f;
    map.counts.assign(40 * 40, 0);
    map.samples.assign(40 * 40, glm::vec2(0.0f));
    const auto put = [&](glm::vec2 point, std::uint32_t count) {
        const glm::ivec2 cell(point / map.cellSize);
        const std::size_t i = static_cast<std::size_t>(cell.x + cell.y * map.dim.x);
        map.counts[i] = count;
        map.samples[i] = point;
    };
    put(near, 3);
    put(far, 9);
    return map;
}

} // namespace

TEST_CASE("clock runs while playing and stops at the end") {
    Round round(small(), kStep);
    round.update(2.0f, nullptr);
    CHECK(round.seconds() == doctest::Approx(2.0f));
    round.applyCounters(counters(95, 5));
    REQUIRE(round.outcome() == RoundOutcome::Won);
    round.update(5.0f, nullptr);
    CHECK(round.seconds() == doctest::Approx(2.0f));
}

TEST_CASE("upgrade money rewards a fast win") {
    RoundSettings settings = small(); // winReward 100, par 240, 1 per second
    Round fast(settings, kStep);
    fast.update(40.0f, nullptr);
    fast.applyCounters(counters(100));
    CHECK(fast.upgradeMoney() == 300);
    Round slow(settings, kStep);
    slow.update(300.0f, nullptr);
    slow.applyCounters(counters(100));
    CHECK(slow.upgradeMoney() == 100);
}

TEST_CASE("towers grow in, then act") {
    Round round(small(), kStep);
    REQUIRE(round.place(TowerKind::Gunner, {500, 500}, true) == 0);
    CHECK(round.effects().towers[0].radius == 0.0f);
    CHECK(round.effects().attractors.empty());
    round.update(0.5f, nullptr);
    CHECK(round.effects().towers[0].radius == doctest::Approx(10.0f));
    CHECK(round.effects().attractors.empty());
    round.update(0.6f, nullptr);
    CHECK(round.effects().towers[0].radius == doctest::Approx(kTowerBodyRadius));
    REQUIRE(round.effects().attractors.size() == 1);
    CHECK(round.effects().attractors[0].radius == doctest::Approx(kTowerPullRadius));
    CHECK(round.effects().attractShare == doctest::Approx(kAttractShare));
}

namespace {

// Runs `round` on with no map until its shots in the air have all landed,
// and returns the blasts they set off.
std::vector<horde::sim::Explosion> landShots(Round& round) {
    for (int i = 0; i < 100 && !round.shots().empty(); ++i) {
        round.update(0.05f, nullptr);
    }
    return round.takeBlasts();
}

} // namespace

TEST_CASE("towers fire at the nearest unit in range, not the biggest crowd") {
    Round round = roundWithGrown(TowerKind::Sniper); // range 400: both in reach
    const glm::vec2 near{660, 500};
    const auto map = crowdMap(near, {180, 500});
    round.update(0.01f, &map);
    REQUIRE(round.shots().size() == 1);
    CHECK(round.shots()[0].to == near);
    CHECK(round.shots()[0].from == glm::vec2(500, 500));
    CHECK(round.shots()[0].kind == TowerKind::Sniper);
    CHECK(round.shots()[0].slot == 0);
}

TEST_CASE("a shot's blast goes off when it lands, not when it's fired") {
    Round round = roundWithGrown(TowerKind::Gunner);
    const glm::vec2 near{660, 500};
    const auto map = crowdMap(near, {180, 500});
    round.update(0.01f, &map);
    CHECK(round.takeBlasts().empty());
    REQUIRE(round.shots().size() == 1);
    const float flight = round.shots()[0].flight;
    CHECK(flight == doctest::Approx(160.0f / towerStats(TowerKind::Gunner, 1).shotSpeed));
    round.update(flight * 0.5f, nullptr);
    CHECK(round.takeBlasts().empty());
    round.update(flight, nullptr);
    const std::vector<horde::sim::Explosion> blasts = round.takeBlasts();
    REQUIRE(blasts.size() == 1);
    CHECK(blasts[0].center == near);
    CHECK(blasts[0].damage == doctest::Approx(10.0f));
    CHECK(blasts[0].radius == doctest::Approx(6.0f));
    CHECK(round.shots().empty());
}

TEST_CASE("a tower waits out its fire period between shots") {
    Round round = roundWithGrown(TowerKind::Gunner);
    const auto map = crowdMap({660, 500}, {180, 500});
    round.update(0.01f, &map);
    round.update(0.01f, &map);
    CHECK(round.shots().size() == 1);
    round.update(towerStats(TowerKind::Gunner, 1).firePeriod, &map);
    CHECK(round.shots().size() == 2);
}

TEST_CASE("towers lead a walking target") {
    Round round = roundWithGrown(TowerKind::Gunner);
    const glm::vec2 near{660, 500};
    auto map = crowdMap(near, {180, 500});
    map.velocities.assign(map.counts.size(), glm::vec2(0.0f));
    const glm::ivec2 cell(near / map.cellSize);
    map.velocities[static_cast<std::size_t>(cell.x + cell.y * map.dim.x)] = {0.0f, 60.0f};
    map.age = 0.1f;
    round.update(0.01f, &map);
    REQUIRE(round.shots().size() == 1);
    const Shot& shot = round.shots()[0];
    // Where it will be after the map's age and the shot's own flight.
    CHECK(shot.to.x == doctest::Approx(near.x));
    CHECK(shot.to.y == doctest::Approx(near.y + 60.0f * (0.1f + shot.flight)).epsilon(0.01));
}

TEST_CASE("a tower with nothing in range holds its fire") {
    Round round = roundWithGrown(TowerKind::Gunner);
    round.update(0.01f, nullptr);
    CHECK(round.takeBlasts().empty());
    const auto map = crowdMap({1200, 1200}, {100, 1200});
    round.update(0.01f, &map);
    CHECK(round.takeBlasts().empty());
}

TEST_CASE("a growing tower does not fire") {
    Round round(small(), kStep);
    REQUIRE(round.place(TowerKind::Gunner, {500, 500}, true) == 0);
    const auto map = crowdMap({560, 500}, {180, 500});
    round.update(0.1f, &map);
    CHECK(round.takeBlasts().empty());
}

TEST_CASE("frost never fires and makes a slow zone") {
    Round round = roundWithGrown(TowerKind::Frost);
    const auto map = crowdMap({560, 500}, {180, 500});
    round.update(0.5f, &map);
    CHECK(round.takeBlasts().empty());
    const horde::sim::CrowdEffects effects = round.effects();
    REQUIRE(effects.slowZones.size() == 1);
    CHECK(effects.slowZones[0].center == glm::vec2(500, 500));
    CHECK(effects.slowZones[0].radius == doctest::Approx(180.0f));
    CHECK(effects.slowZones[0].speedFactor == doctest::Approx(0.4f));
}

TEST_CASE("abilities cost money and cool down") {
    Round round(small(), kStep);
    CHECK(round.useAbility(AbilityKind::Bomb, {300, 300}));
    CHECK(round.money() == 350);
    std::vector<horde::sim::Explosion> blasts = round.takeBlasts();
    REQUIRE(blasts.size() == 1);
    CHECK(blasts[0].radius == doctest::Approx(80.0f));
    CHECK(blasts[0].center == glm::vec2(300, 300));
    CHECK(round.abilityCooldown(AbilityKind::Bomb) == doctest::Approx(8.0f));
    CHECK_FALSE(round.useAbility(AbilityKind::Bomb, {300, 300}));
    CHECK(round.money() == 350);
    round.update(8.0f, nullptr);
    CHECK(round.useAbility(AbilityKind::Bomb, {300, 300}));
}

TEST_CASE("an ability the player cannot afford is refused") {
    RoundSettings settings = small();
    settings.startMoney = 40;
    Round round(settings, kStep);
    CHECK_FALSE(round.useAbility(AbilityKind::Bomb, {300, 300}));
    CHECK(round.takeBlasts().empty());
    CHECK(round.abilityCooldown(AbilityKind::Bomb) == 0.0f);
}

TEST_CASE("click is free with a short cooldown") {
    Round round(small(), kStep);
    CHECK(round.click({200, 200}));
    std::vector<horde::sim::Explosion> blasts = round.takeBlasts();
    REQUIRE(blasts.size() == 1);
    CHECK(blasts[0].radius == doctest::Approx(12.0f));
    CHECK(blasts[0].damage == doctest::Approx(10.0f));
    CHECK(round.money() == 400);
    CHECK_FALSE(round.click({200, 200}));
    round.update(0.25f, nullptr);
    CHECK(round.click({200, 200}));
}

TEST_CASE("gravity pulls without damage") {
    Round round(small(), kStep);
    REQUIRE(round.useAbility(AbilityKind::Gravity, {300, 300}));
    const std::vector<horde::sim::Explosion> blasts = round.takeBlasts();
    REQUIRE(blasts.size() == 1);
    CHECK(blasts[0].strength < 0.0f);
    CHECK(blasts[0].damage == 0.0f);
    CHECK(blasts[0].duration == doctest::Approx(3.0f));
}

TEST_CASE("ended rounds ignore clicks and abilities") {
    Round round(small(), kStep);
    round.applyCounters(counters(90, 10));
    REQUIRE(round.outcome() == RoundOutcome::Lost);
    CHECK_FALSE(round.click({200, 200}));
    CHECK_FALSE(round.useAbility(AbilityKind::Bomb, {200, 200}));
    CHECK(round.takeBlasts().empty());
}

TEST_CASE("the click and single-target towers hit evenly across their blast") {
    Round round = roundWithGrown(TowerKind::Gunner);
    REQUIRE(round.click({200, 200}));
    std::vector<horde::sim::Explosion> blasts = round.takeBlasts();
    REQUIRE(blasts.size() == 1);
    CHECK(blasts[0].falloff == horde::sim::ExplosionFalloff::Constant);
    const auto map = crowdMap({660, 500}, {180, 500});
    round.update(0.01f, &map);
    blasts = landShots(round);
    REQUIRE(blasts.size() == 1);
    CHECK(blasts[0].falloff == horde::sim::ExplosionFalloff::Constant);
}

TEST_CASE("bomb damage still fades toward its edge") {
    Round round(small(), kStep);
    REQUIRE(round.useAbility(AbilityKind::Bomb, {300, 300}));
    CHECK(round.takeBlasts()[0].falloff == horde::sim::ExplosionFalloff::Linear);
}

TEST_CASE("towers leave a unit-wide gap between them") {
    Round round(small(), kStep);
    REQUIRE(round.place(TowerKind::Gunner, {500, 500}, true) == 0);
    CHECK(round.canPlace(TowerKind::Gunner, {544, 500}, true) == PlaceError::Blocked);
    CHECK(round.canPlace(TowerKind::Gunner, {545, 500}, true) == PlaceError::None);
}
