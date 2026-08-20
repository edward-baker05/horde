#include "logic/LevelFiles.hpp"

#include <algorithm>
#include <cctype>

#include "core/Paths.hpp"

namespace horde::logic {

std::filesystem::path levelsDirectory() {
    return paths::asset("levels");
}

std::vector<std::filesystem::path> listLevels() {
    std::vector<std::filesystem::path> found;

    std::error_code code;
    const std::filesystem::path directory = levelsDirectory();
    if (!std::filesystem::is_directory(directory, code)) {
        return found;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, code)) {
        if (!entry.is_regular_file(code)) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        const std::string suffix = kLevelExtension;
        if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            found.push_back(entry.path());
        }
    }

    std::sort(found.begin(), found.end());
    return found;
}

std::string levelDisplayName(const std::filesystem::path& path) {
    std::string name = path.filename().string();
    const std::string suffix = kLevelExtension;
    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
        name.erase(name.size() - suffix.size());
    }
    return name;
}

std::filesystem::path levelPathForName(const std::string& name) {
    // Keep only characters that cannot form a path, so a name typed into the
    // save box can never write outside the levels directory.
    std::string safe;
    safe.reserve(name.size());
    for (const char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) != 0 || c == '-' || c == '_' || c == ' ') {
            safe.push_back(c);
        }
    }

    if (safe.empty()) {
        safe = "untitled";
    }

    return levelsDirectory() / (safe + kLevelExtension);
}

} // namespace horde::logic
