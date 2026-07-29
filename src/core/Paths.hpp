#pragma once

#include <filesystem>
#include <string>

namespace horde::paths {

// Directory containing the executable, from SDL_GetBasePath().
//
// Every runtime file is resolved relative to this rather than the working
// directory, so the game runs correctly however it was launched — from an IDE,
// a shortcut, or a shell in an unrelated directory.
const std::filesystem::path& base();

// base()/assets/<relative>
std::filesystem::path asset(const std::string& relative);

// base()/shaders/<relative>
std::filesystem::path shader(const std::string& relative);

} // namespace horde::paths
