#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "scene/LevelScene.hpp"

namespace horde::scene {

// A named, shareable set of LevelOptions: a JSON file in assets/presets/,
// "<name>.preset.json". Presets are how test setups get passed around: anyone
// with the build can play one from the main menu, the level panel, or
// `horde --preset <name>`, and benchmark it with `--bench --preset <name>`.
//
// Every key is optional; anything left out keeps its default. Unknown keys are
// an error, so a typo fails loudly instead of silently testing the default.
//
//   description          shown next to the preset in menus
//   level                a level in assets/levels by name ("bench_walls"); omit for the built-in level
//   physics              a registered physics system ("gpu-collide")
//   units                int
//   spawn                "markers" | "random" | "grid" | "column"
//   spawnRate            units per second at the spawns; 0 = all at once
//   respawnAtExit        bool
//   speed                unit speed
//   physicsRate          0 (once per frame), 60 or 120
//   maxHp                int
//   exitDamage           float
//   solver               "relaxed" | "average"
//   collisionIterations  int, passes per physics step
//   relaxation           float, relaxed solver
//   omega                float 1-2, average solver
//   seed                 int
struct Preset {
    std::string name;
    std::string description;
    LevelOptions options;
    // Which keys the file set, so callers know what it overrides.
    bool setsSpawn = false;
    bool setsPhysicsRate = false;
};

inline constexpr const char* kPresetExtension = ".preset.json";

std::filesystem::path presetsDirectory();

// Every preset file, sorted by name. Empty if there are none.
std::vector<std::filesystem::path> listPresets();

// "assets/presets/crowd.preset.json" -> "crowd"
std::string presetName(const std::filesystem::path& path);

// Loads the preset named `name` from presetsDirectory(), or the file at
// `name` if it is a path to one. Applies it on top of `base`.
// Returns false and fills `error` on failure.
bool loadPreset(const std::string& name, const LevelOptions& base, Preset& out, std::string& error);

} // namespace horde::scene
