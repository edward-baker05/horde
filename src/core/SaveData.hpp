#pragma once

#include <cstdint>
#include <filesystem>

// What carries over between rounds. For now only the upgrade money rounds
// pay out; the campaign will spend it.
namespace horde::save {

struct SaveData {
    std::int64_t upgradeMoney = 0;
};

// A missing or unreadable file is an empty save, never an error: losing
// progress is bad, refusing to start is worse.
SaveData load(const std::filesystem::path& path);
bool write(const SaveData& data, const std::filesystem::path& path);
// save.json in the user's preference directory.
std::filesystem::path defaultPath();

} // namespace horde::save
