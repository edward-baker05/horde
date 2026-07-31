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

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="${ROOT}/tools/.shadercross"
PREFIX="${ROOT}/tools"

SHADERCROSS_REF="${SHADERCROSS_REF:-main}"

if ! command -v cmake >/dev/null 2>&1; then
    echo "error: cmake not found on PATH" >&2
    exit 1
fi

if ! command -v git >/dev/null 2>&1; then
    echo "error: git not found on PATH" >&2
    exit 1
fi

# shadercross links against SDL3. If there is no system SDL3 to find, build one
# into the same private prefix rather than requiring a root install.
if ! pkg-config --exists sdl3 2>/dev/null; then
    if [ ! -x "${PREFIX}/bin/../lib64/libSDL3.so" ] && [ ! -f "${PREFIX}/lib/libSDL3.so" ] &&
       [ ! -f "${PREFIX}/lib64/libSDL3.so" ]; then
        echo "==> No system SDL3 found; building one into ${PREFIX}"

        if [ ! -d "${WORK}/sdl" ]; then
            git clone --depth 1 --branch "${SDL_REF:-release-3.4.12}" \
                https://github.com/libsdl-org/SDL.git "${WORK}/sdl"
        fi

        cmake -S "${WORK}/sdl" -B "${WORK}/sdl-build" \
            -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
            -DSDL_TEST_LIBRARY=OFF \
            -DSDL_X11=OFF \
            -DSDL_WAYLAND=OFF \
            -DSDL_EXAMPLES=OFF
        cmake --build "${WORK}/sdl-build"
        cmake --install "${WORK}/sdl-build"
    fi

    export CMAKE_PREFIX_PATH="${PREFIX}${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
fi

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

echo "==> Configuring"
cmake -S "${WORK}/src" -B "${WORK}/build" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
    -DSDLSHADERCROSS_VENDORED=ON \
    -DSDLSHADERCROSS_CLI=ON \
    -DSDLSHADERCROSS_SHARED=OFF \
    -DSDLSHADERCROSS_STATIC=ON \
    -DSDLSHADERCROSS_TESTS=OFF \
    -DSDLSHADERCROSS_INSTALL=ON \
    -DSPIRV_WERROR=OFF \
    -DSPIRV_CROSS_WERROR=OFF \
    -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib64;$ORIGIN/../lib' \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON

# Two things above are not obvious:
#
#   SPIRV_WERROR / SPIRV_CROSS_WERROR - SPIRV-Tools and SPIRV-Cross build with
#   -Werror by default, and newer compilers (GCC 16) emit warnings their code
#   predates. None of it is our code, so downgrade to warnings rather than
#   pinning an older toolchain.
#
#   CMAKE_INSTALL_RPATH - the CLI loads libspirv-cross and libdxcompiler from
#   the same prefix at runtime. Without an $ORIGIN-relative rpath the install
#   step strips the rpath entirely and the binary cannot find its own libraries.
    # SPIRV-Tools and SPIRV-Cross default to -Werror, and newer compilers
    # (GCC 16) emit warnings their code predates. Nothing here is our code, so
    # turn those into warnings rather than pinning an old toolchain.

echo "==> Building (this is the long part)"
cmake --build "${WORK}/build"

echo "==> Installing into ${PREFIX}/bin"
cmake --install "${WORK}/build"

echo
echo "Done. shadercross is at ${PREFIX}/bin/shadercross"
echo "Reconfigure the project (delete build/ or re-run cmake --preset ...) to pick it up."
