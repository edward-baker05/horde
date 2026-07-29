#include "core/Paths.hpp"

#include <SDL3/SDL_filesystem.h>

namespace horde::paths {

const std::filesystem::path& base() {
    static const std::filesystem::path cached = [] {
        // SDL owns this string; it is valid for the lifetime of the program and
        // must not be freed.
        const char* path = SDL_GetBasePath();
        return path != nullptr ? std::filesystem::path(path) : std::filesystem::current_path();
    }();

    return cached;
}

std::filesystem::path asset(const std::string& relative) {
    return base() / "assets" / relative;
}

std::filesystem::path shader(const std::string& relative) {
    return base() / "shaders" / relative;
}

} // namespace horde::paths
