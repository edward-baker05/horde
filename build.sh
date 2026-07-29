#!/usr/bin/env bash
# Configure, build, and run horde for the current platform.
# Usage: ./build.sh [debug|release]
set -euo pipefail

MODE="${1:-debug}"
case "$MODE" in
  debug|release) ;;
  *)
    echo "Usage: $0 [debug|release]" >&2
    exit 1
    ;;
esac

case "$(uname -s)" in
  Darwin) PLATFORM=macos ;;
  Linux)  PLATFORM=linux ;;
  *)
    echo "Unsupported platform: $(uname -s). See README.md for Windows instructions (build.ps1)." >&2
    exit 1
    ;;
esac

PRESET="${PLATFORM}-${MODE}"

cmake --preset "$PRESET"
cmake --build --preset "$PRESET"

# No need to cd first: assets and shaders are resolved relative to the
# executable via SDL_GetBasePath(), not the working directory.
exec "./build/${PRESET}/bin/horde"
