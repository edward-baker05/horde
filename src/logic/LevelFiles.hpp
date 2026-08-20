#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace horde::logic {

// The suffix identifying a level file. Doubled extension so editors syntax
// highlight it as JSON while it remains recognisably a level.
inline constexpr const char* kLevelExtension = ".level.json";

// base()/assets/levels — the one directory levels live in. It ships next to the
// executable via the horde_runtime_files build step.
std::filesystem::path levelsDirectory();

// Every level file in levelsDirectory(), sorted by filename. Empty if the
// directory does not exist; never throws.
std::vector<std::filesystem::path> listLevels();

// "assets/levels/arena.level.json" -> "arena". For display and for pre-filling
// the save box.
std::string levelDisplayName(const std::filesystem::path& path);

// "arena" -> levelsDirectory()/"arena.level.json". Strips path separators and
// anything else that would let a name escape the levels directory.
std::filesystem::path levelPathForName(const std::string& name);

} // namespace horde::logic
