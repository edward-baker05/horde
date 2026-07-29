#!/usr/bin/env bash
#
# Builds the SDL_shadercross CLI into tools/bin/, which is where
# cmake/Shaders.cmake looks for it.
#
# You only need this if you are EDITING a shader. Building the game itself uses
# the compiled artifacts committed under shaders/compiled/ and needs no shader
# toolchain.
#
# This is slow the first time — SDL_shadercross has no releases and no packaged
# dependencies, so it vendors and compiles DirectXShaderCompiler, which is an
# LLVM fork. Expect a large clone and a long build. It is a one-time cost.
#
# Note: Windows is the only platform that can produce SIGNED DXIL, because the
# signing library dxil.dll ships only on Windows.

set -euo pipefail

SHADERCROSS_REF="${1:-${SHADERCROSS_REF:-main}}"
SDL_REF="${SDL_REF:-release-3.4.12}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="${ROOT}/tools/.shadercross"
PREFIX="${ROOT}/tools"

# 1. Detect platform
IS_WIN=false
case "$(uname -s)" in
    CYGWIN*|MINGW*|MSYS*) IS_WIN=true ;;
esac

# 2. Check prerequisites
for tool in cmake git; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: $tool not found on PATH" >&2
        exit 1
    fi
done

# 3. Assemble platform-specific CMake flags & generator selection
CMAKE_ARGS=()
if command -v ninja >/dev/null 2>&1; then
    CMAKE_ARGS+=(-G Ninja)
fi

if [ "$IS_WIN" = false ]; then
    # Prevent rpath stripping on ELF binaries so shadercross can find sibling libs
    CMAKE_ARGS+=(
        -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib64;$ORIGIN/../lib'
        -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
    )
fi

# 4. Ensure SDL3 is present (build privately if not found)
has_sdl3() {
    if pkg-config --exists sdl3 2>/dev/null; then
        return 0
    fi
    # Check private prefix for existing dynamic/static builds
    local matches
    matches=$(find "${PREFIX}" \( -name "libSDL3.so*" -o -name "libSDL3.dylib*" -o -name "SDL3.dll" -o -name "SDL3.lib" -o -name "libSDL3.a" -o -name "libSDL3.dll.a" \) 2>/dev/null)
    [ -n "$matches" ]
}

if ! has_sdl3; then
    echo "==> No system SDL3 found; building one into ${PREFIX}"

    if [ ! -d "${WORK}/sdl" ]; then
        git clone --depth 1 --branch "${SDL_REF}" \
            https://github.com/libsdl-org/SDL.git "${WORK}/sdl"
    fi

    cmake -S "${WORK}/sdl" -B "${WORK}/sdl-build" \
        "${CMAKE_ARGS[@]}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
        -DSDL_TEST_LIBRARY=OFF \
        -DSDL_EXAMPLES=OFF

    cmake --build "${WORK}/sdl-build" --config Release
    cmake --install "${WORK}/sdl-build" --config Release
fi

export CMAKE_PREFIX_PATH="${PREFIX}${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"

# 5. Clone/update SDL_shadercross
if [ ! -d "${WORK}/src" ]; then
    echo "==> Cloning SDL_shadercross (${SHADERCROSS_REF}) and its vendored dependencies"
    echo "    This clones DirectXShaderCompiler and will take a while."
    git clone --recurse-submodules --shallow-submodules --depth 1 \
        --branch "${SHADERCROSS_REF}" \
        https://github.com/libsdl-org/SDL_shadercross.git "${WORK}/src"
else
    echo "==> Reusing existing clone at ${WORK}/src"
    git -C "${WORK}/src" submodule update --init --recursive --depth 1
fi

# 6. Configure, Build, and Install SDL_shadercross
echo "==> Configuring"
cmake -S "${WORK}/src" -B "${WORK}/build" \
    "${CMAKE_ARGS[@]}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
    -DSDLSHADERCROSS_VENDORED=ON \
    -DSDLSHADERCROSS_CLI=ON \
    -DSDLSHADERCROSS_SHARED=OFF \
    -DSDLSHADERCROSS_STATIC=ON \
    -DSDLSHADERCROSS_TESTS=OFF \
    -DSDLSHADERCROSS_INSTALL=ON \
    -DSPIRV_WERROR=OFF \
    -DSPIRV_CROSS_WERROR=OFF

echo "==> Building (this is the long part)"
cmake --build "${WORK}/build" --config Release

echo "==> Installing into ${PREFIX}/bin"
cmake --install "${WORK}/build" --config Release

BIN_NAME="shadercross"
if [ "$IS_WIN" = true ]; then
    BIN_NAME="shadercross.exe"
fi

echo
echo "Done. shadercross is at ${PREFIX}/bin/${BIN_NAME}"
echo "Reconfigure the project to pick it up."