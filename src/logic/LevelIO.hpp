#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "logic/Level.hpp"

namespace horde::logic {

// The schema version this build writes, and the only one it reads.
inline constexpr int kLevelFormatVersion = 1;

// Reads a level from disk.
//
// Returns nullopt on any failure — missing file, malformed JSON, unrecognised
// version, missing required key — writing a human-readable reason into `error`
// when it is non-null. No exception ever escapes: callers are expected to fall
// back rather than to catch.
std::optional<Level> loadLevel(const std::filesystem::path& path, std::string* error);

// Writes a level to disk, creating parent directories as needed. Returns false
// and fills `error` on failure.
//
// This does NOT validate. Callers that must not write a broken level are
// responsible for calling logic::validate first.
bool saveLevel(const Level& level, const std::filesystem::path& path, std::string* error);

} // namespace horde::logic
