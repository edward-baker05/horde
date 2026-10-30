#include "scene/Presets.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <set>

#include "core/Paths.hpp"
#include "logic/LevelFiles.hpp"

namespace horde::scene {

namespace {

using json = nlohmann::json;

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() > suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Reads `key` into `out` when present, checking its type. Returns false (with
// `error` set) only when the key is present but wrong.
template <typename T>
bool read(const json& root, const char* key, T& out, std::string& error) {
    if (!root.contains(key)) {
        return true;
    }
    try {
        out = root.at(key).get<T>();
        return true;
    } catch (const json::exception&) {
        error = std::string("'") + key + "' has the wrong type";
        return false;
    }
}

} // namespace

std::filesystem::path presetsDirectory() {
    return paths::asset("presets");
}

std::vector<std::filesystem::path> listPresets() {
    std::vector<std::filesystem::path> found;
    std::error_code code;
    if (!std::filesystem::is_directory(presetsDirectory(), code)) {
        return found;
    }
    for (const auto& entry : std::filesystem::directory_iterator(presetsDirectory(), code)) {
        if (entry.is_regular_file(code) && endsWith(entry.path().filename().string(), kPresetExtension)) {
            found.push_back(entry.path());
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

std::string presetName(const std::filesystem::path& path) {
    std::string name = path.filename().string();
    if (endsWith(name, kPresetExtension)) {
        name.erase(name.size() - std::string(kPresetExtension).size());
    }
    return name;
}

bool loadPreset(const std::string& name, const LevelOptions& base, Preset& out, std::string& error) {
    std::filesystem::path path = presetsDirectory() / (name + kPresetExtension);
    std::error_code code;
    if (!std::filesystem::exists(path, code) && std::filesystem::exists(name, code)) {
        path = name;
    }

    std::ifstream file(path);
    if (!file) {
        error = "no preset '" + name + "' (looked for " + path.generic_string() + ")";
        return false;
    }

    json root;
    try {
        root = json::parse(file);
    } catch (const json::exception& e) {
        error = path.generic_string() + ": malformed JSON: " + e.what();
        return false;
    }
    if (!root.is_object()) {
        error = path.generic_string() + ": a preset is a JSON object";
        return false;
    }

    static const std::set<std::string> known = {
        "description", "level",      "physics", "units",      "spawn",      "spawnRate",
        "respawnAtExit", "speed",    "physicsRate", "maxHp",  "exitDamage",
        "solver",      "collisionIterations", "relaxation", "omega", "seed"};
    for (const auto& [key, value] : root.items()) {
        if (!known.contains(key)) {
            error = path.generic_string() + ": unknown key '" + key + "'";
            return false;
        }
    }

    Preset preset;
    preset.name = presetName(path);
    preset.options = base;
    LevelOptions& o = preset.options;
    const auto fail = [&](const std::string& what) {
        error = path.generic_string() + ": " + what;
        return false;
    };

    std::string text;
    if (!read(root, "description", preset.description, error)) return fail(error);

    if (root.contains("level")) {
        if (!read(root, "level", text, error)) return fail(error);
        o.levelPath = logic::levelPathForName(text);
        o.level.reset();
        if (!std::filesystem::exists(o.levelPath, code)) return fail("no level '" + text + "'");
    }
    if (root.contains("physics")) {
        if (!read(root, "physics", o.physics, error)) return fail(error);
        if (sim::createPhysics(o.physics) == nullptr) return fail("unknown physics '" + o.physics + "'");
    }
    if (!read(root, "units", o.unitCount, error)) return fail(error);
    if (root.contains("spawn")) {
        if (!read(root, "spawn", text, error)) return fail(error);
        if (!sim::parseSpawnPattern(text, o.spawn)) return fail("unknown spawn '" + text + "'");
        preset.setsSpawn = true;
    }
    if (!read(root, "spawnRate", o.spawnRate, error)) return fail(error);
    if (!read(root, "respawnAtExit", o.respawnAtExit, error)) return fail(error);
    if (!read(root, "speed", o.maxSpeed, error)) return fail(error);
    if (root.contains("physicsRate")) {
        if (!read(root, "physicsRate", o.physicsRate, error)) return fail(error);
        if (o.physicsRate != 0.0f && o.physicsRate != 60.0f && o.physicsRate != 120.0f) {
            return fail("physicsRate must be 0, 60 or 120");
        }
        preset.setsPhysicsRate = true;
    }
    if (!read(root, "maxHp", o.health.maxHp, error)) return fail(error);
    if (o.health.maxHp == 0) return fail("maxHp must be at least 1");
    if (!read(root, "exitDamage", o.health.exitDamage, error)) return fail(error);

    if (root.contains("solver")) {
        if (!read(root, "solver", text, error)) return fail(error);
        if (!sim::parseSolver(text, o.collision.solver)) return fail("unknown solver '" + text + "'");
    }
    if (!read(root, "collisionIterations", o.collision.iterations, error)) return fail(error);
    if (!read(root, "relaxation", o.collision.relaxation, error)) return fail(error);
    if (!read(root, "omega", o.collision.averageOmega, error)) return fail(error);
    if (o.collision.averageOmega <= 0.0f || o.collision.averageOmega > 2.0f) return fail("omega must be in (0, 2]");
    if (!read(root, "seed", o.seed, error)) return fail(error);

    o.presetName = preset.name;
    out = std::move(preset);
    return true;
}

} // namespace horde::scene
