#include <doctest/doctest.h>

#include "logic/Round.hpp"

using namespace horde::logic;

namespace {

RoundSettings small() {
    RoundSettings settings;
    settings.totalUnits = 100;
    settings.leakLimit = 10;
    settings.startMoney = 400;
    settings.killReward = 2;
    return settings;
}

RoundCounters counters(std::uint32_t killed, std::uint32_t leaked = 0) {
    return {killed, leaked};
}

// A round past its prep phase.
Round started(const RoundSettings& settings = small()) {
    Round round(settings);
    round.start();
    return round;
}

} // namespace

TEST_CASE("prep holds the clock, reports and the click") {
    Round round(small());
    CHECK_FALSE(round.started());
    round.update(5.0f);
    CHECK(round.seconds() == doctest::Approx(0.0f));
    round.applyCounters(counters(5));
    CHECK(round.killed() == 0);
    CHECK(round.money() == 400);
    CHECK_FALSE(round.click({200, 200}));
    CHECK(round.takeBlasts().empty());

    round.start();
    CHECK(round.started());
    round.update(1.0f);
    CHECK(round.seconds() == doctest::Approx(1.0f));
}

TEST_CASE("turrets are paid for in prep and in play") {
    Round round(small());
    CHECK(round.spend(150));
    CHECK(round.money() == 250);
    round.start();
    CHECK(round.spend(250));
    CHECK(round.money() == 0);
}

TEST_CASE("spending more than the round has is refused") {
    Round round = started();
    CHECK_FALSE(round.spend(401));
    CHECK(round.money() == 400);
    CHECK_FALSE(round.spend(-5));
    CHECK(round.money() == 400);
}

TEST_CASE("kills pay and stale reports are ignored") {
    Round round = started();
    round.applyCounters(counters(5));
    CHECK(round.money() == 410);
    CHECK(round.killed() == 5);
    round.applyCounters(counters(3));
    CHECK(round.money() == 410);
    CHECK(round.killed() == 5);
}

TEST_CASE("leak limit loses, even on the last kill") {
    Round round = started();
    round.applyCounters(counters(90, 10));
    CHECK(round.outcome() == RoundOutcome::Lost);
    CHECK(round.upgradeMoney() == 0);
}

TEST_CASE("all dealt with under the limit wins") {
    Round round = started();
    round.applyCounters(counters(94, 5));
    CHECK(round.outcome() == RoundOutcome::Playing);
    round.applyCounters(counters(95, 5));
    CHECK(round.outcome() == RoundOutcome::Won);
}

TEST_CASE("ended rounds ignore actions") {
    Round round = started();
    round.applyCounters(counters(95, 5));
    REQUIRE(round.outcome() == RoundOutcome::Won);
    const int money = round.money();
    CHECK_FALSE(round.spend(10));
    round.applyCounters(counters(100, 9));
    CHECK(round.money() == money);
    CHECK(round.outcome() == RoundOutcome::Won);
}

TEST_CASE("spawn rate rises with kills") {
    Round round = started(); // base 30, boost 4, total 100
    CHECK(round.spawnRate() == doctest::Approx(30.0f));
    round.applyCounters(counters(50));
    CHECK(round.spawnRate() == doctest::Approx(90.0f));
    round.applyCounters(counters(100));
    CHECK(round.spawnRate() == doctest::Approx(150.0f));
}

TEST_CASE("clock runs while playing and stops at the end") {
    Round round = started();
    round.update(2.0f);
    CHECK(round.seconds() == doctest::Approx(2.0f));
    round.applyCounters(counters(95, 5));
    REQUIRE(round.outcome() == RoundOutcome::Won);
    round.update(5.0f);
    CHECK(round.seconds() == doctest::Approx(2.0f));
}

TEST_CASE("upgrade money rewards a fast win") {
    RoundSettings settings = small(); // winReward 100, par 240, 1 per second
    Round fast = started(settings);
    fast.update(40.0f);
    fast.applyCounters(counters(100));
    CHECK(fast.upgradeMoney() == 300);

    Round slow = started(settings);
    slow.update(300.0f);
    slow.applyCounters(counters(100));
    CHECK(slow.upgradeMoney() == 100);
}

TEST_CASE("click is free with a short cooldown") {
    Round round = started();
    CHECK(round.click({200, 200}));
    std::vector<horde::sim::Explosion> blasts = round.takeBlasts();
    REQUIRE(blasts.size() == 1);
    CHECK(blasts[0].radius == doctest::Approx(kClickRadius));
    CHECK(blasts[0].damage == doctest::Approx(kClickDamage));
    CHECK(round.money() == 400);
    CHECK_FALSE(round.click({200, 200}));
    round.update(kClickCooldown);
    CHECK(round.click({200, 200}));
}

TEST_CASE("the click hits evenly across its blast") {
    Round round = started();
    REQUIRE(round.click({200, 200}));
    const std::vector<horde::sim::Explosion> blasts = round.takeBlasts();
    REQUIRE(blasts.size() == 1);
    CHECK(blasts[0].falloff == horde::sim::ExplosionFalloff::Constant);
    CHECK(blasts[0].strength == doctest::Approx(0.0f));
}

TEST_CASE("ended rounds ignore clicks") {
    Round round = started();
    round.applyCounters(counters(90, 10));
    REQUIRE(round.outcome() == RoundOutcome::Lost);
    CHECK_FALSE(round.click({200, 200}));
    CHECK(round.takeBlasts().empty());
}
