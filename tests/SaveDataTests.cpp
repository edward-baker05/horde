#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "core/SaveData.hpp"

using namespace horde;

namespace {
std::filesystem::path tempSave(const char* name) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / (std::string("horde_save_") + name + ".json");
    std::filesystem::remove(path);
    return path;
}
} // namespace

TEST_CASE("a missing save file has no upgrade money") {
    CHECK(save::load(tempSave("missing")).upgradeMoney == 0);
}

TEST_CASE("upgrade money survives a save and load") {
    const std::filesystem::path path = tempSave("roundtrip");
    REQUIRE(save::write({1234}, path));
    CHECK(save::load(path).upgradeMoney == 1234);
}

TEST_CASE("a corrupt save file loads as empty") {
    const std::filesystem::path path = tempSave("corrupt");
    std::ofstream(path) << "not json";
    CHECK(save::load(path).upgradeMoney == 0);
}
