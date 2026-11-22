#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "logic/Level.hpp"
#include "logic/LevelIO.hpp"

using namespace horde::logic;

namespace {

std::filesystem::path tempLevel(const char* name) {
    return std::filesystem::temp_directory_path() / (std::string("horde_test_") + name + ".level.json");
}

// A minimal level file written by hand, with `extra` spliced in before the
// closing brace (empty, or `,"round":{...}`).
std::filesystem::path writeRaw(const char* name, int version, const std::string& extra) {
    const std::filesystem::path path = tempLevel(name);
    std::ofstream out(path, std::ios::trunc);
    out << "{\"version\":" << version << ",\"size\":[100,100],\"backgroundColor\":[1,2,3],"
        << "\"markers\":[{\"kind\":\"spawn\",\"edge\":\"west\",\"offset\":0,\"length\":50},"
        << "{\"kind\":\"exit\",\"edge\":\"east\",\"offset\":0,\"length\":50}]" << extra << "}";
    return path;
}

} // namespace

TEST_CASE("level round settings round-trip") {
    Level level = makeDefaultLevel();
    level.round.totalUnits = 1234;
    level.round.unitHp = 7;
    level.round.leakLimit = 33;
    level.round.startMoney = 999;
    level.round.killReward = 3;
    level.round.baseSpawnRate = 12.5f;
    level.round.spawnBoost = 2.5f;
    level.round.winReward = 250;
    level.round.parSeconds = 90.0f;
    level.round.bonusPerSecond = 0.5f;

    const std::filesystem::path path = tempLevel("roundtrip");
    std::string error;
    REQUIRE(saveLevel(level, path, &error));
    const std::optional<Level> loaded = loadLevel(path, &error);
    REQUIRE(loaded.has_value());
    const RoundSettings& r = loaded->round;
    CHECK(r.totalUnits == 1234);
    CHECK(r.unitHp == 7);
    CHECK(r.leakLimit == 33);
    CHECK(r.startMoney == 999);
    CHECK(r.killReward == 3);
    CHECK(r.baseSpawnRate == doctest::Approx(12.5f));
    CHECK(r.spawnBoost == doctest::Approx(2.5f));
    CHECK(r.winReward == 250);
    CHECK(r.parSeconds == doctest::Approx(90.0f));
    CHECK(r.bonusPerSecond == doctest::Approx(0.5f));
}

TEST_CASE("version 2 level without round loads defaults") {
    std::string error;
    const std::optional<Level> loaded = loadLevel(writeRaw("v2", 2, ""), &error);
    REQUIRE_MESSAGE(loaded.has_value(), error);
    CHECK(loaded->round.totalUnits == 10000);
    CHECK(loaded->round.leakLimit == 200);
    CHECK(loaded->round.startMoney == 400);
}

TEST_CASE("missing round keys take their defaults") {
    std::string error;
    const std::optional<Level> loaded = loadLevel(writeRaw("partial", 3, ",\"round\":{\"totalUnits\":50}"), &error);
    REQUIRE_MESSAGE(loaded.has_value(), error);
    CHECK(loaded->round.totalUnits == 50);
    CHECK(loaded->round.unitHp == 10);
    CHECK(loaded->round.baseSpawnRate == doctest::Approx(30.0f));
}

TEST_CASE("invalid round settings are rejected") {
    const char* bad[] = {
        ",\"round\":{\"totalUnits\":0}",     ",\"round\":{\"leakLimit\":0}",   ",\"round\":{\"unitHp\":0}",
        ",\"round\":{\"baseSpawnRate\":-1}", ",\"round\":{\"startMoney\":-5}", ",\"round\":\"nope\"",
    };
    for (const char* extra : bad) {
        std::string error;
        const std::optional<Level> loaded = loadLevel(writeRaw("bad", 3, extra), &error);
        CHECK_MESSAGE(!loaded.has_value(), extra);
        CHECK_MESSAGE(!error.empty(), extra);
    }
}
