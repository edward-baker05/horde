#include "core/SaveData.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <fstream>
#include <nlohmann/json.hpp>

namespace horde::save {

using nlohmann::json;

SaveData load(const std::filesystem::path& path) {
    SaveData data;
    std::ifstream stream(path);
    if (!stream) {
        return data;
    }
    const json root = json::parse(stream, nullptr, false);
    if (root.is_object() && root.contains("upgradeMoney") && root["upgradeMoney"].is_number_integer()) {
        data.upgradeMoney = root["upgradeMoney"].get<std::int64_t>();
    }
    return data;
}

bool write(const SaveData& data, const std::filesystem::path& path) {
    json root;
    root["upgradeMoney"] = data.upgradeMoney;
    std::error_code code;
    std::filesystem::create_directories(path.parent_path(), code);
    std::ofstream stream(path, std::ios::trunc);
    if (!stream) {
        return false;
    }
    stream << root.dump(2) << '\n';
    return static_cast<bool>(stream);
}

std::filesystem::path defaultPath() {
    char* dir = SDL_GetPrefPath("horde", "horde");
    std::filesystem::path path = dir ? std::filesystem::path(dir) : std::filesystem::current_path();
    SDL_free(dir);
    return path / "save.json";
}

} // namespace horde::save
