# Command reference

Quick lookup for everyday commands. See `README.md` for the one-time setup and
the reasoning behind any of this.

---

## Everyday

| What | Linux / macOS | Windows |
| --- | --- | --- |
| Configure, build and run | `./build.sh debug` | `.\build.ps1 debug` |
| Same, optimized | `./build.sh release` | `.\build.ps1 release` |

That is all you need day to day. Everything below is the same thing broken into
steps, or something you do rarely.

---

## Build steps individually

Replace `linux` with `macos` on macOS. On Windows the configure preset is
`windows` for both configurations.

```sh
# Configure (only needed on first build, or after editing CMakeLists.txt)
cmake --preset linux-debug

# Build
cmake --build --preset linux-debug

# Run — works from any directory; assets resolve relative to the executable
./build/linux-debug/bin/horde
```

```powershell
cmake --preset windows
cmake --build --preset windows-debug
.\build\windows\bin\Debug\horde.exe
```

Available presets:

| Configure preset | Build preset | Output |
| --- | --- | --- |
| `linux-debug` | `linux-debug` | `build/linux-debug/bin/horde` |
| `linux-release` | `linux-release` | `build/linux-release/bin/horde` |
| `macos-debug` | `macos-debug` | `build/macos-debug/bin/horde` |
| `macos-release` | `macos-release` | `build/macos-release/bin/horde` |
| `windows` | `windows-debug` | `build\windows\bin\Debug\horde.exe` |
| `windows` | `windows-release` | `build\windows\bin\Release\horde.exe` |

```sh
# Rebuild one target
cmake --build --preset linux-debug --target horde

# Parallel build (Ninja already does this; useful on the VS generator)
cmake --build --preset linux-debug --parallel 8

# Start completely fresh
rm -rf build && cmake --preset linux-debug && cmake --build --preset linux-debug
```

---

## Shaders

Shaders are authored in `shaders/src/*.hlsl`. The compiled output in
`shaders/compiled/` is **committed**, so a normal build needs no shader
toolchain. You only need the steps below to *change* a shader.

```sh
# One-time: build the shadercross CLI into tools/bin/ (slow, builds DXC)
./tools/build_shadercross.sh          # Windows: .\tools\build_shadercross.ps1

# Reconfigure so CMake picks it up
cmake --preset linux-debug

# From here, shaders recompile as part of any normal build
cmake --build --preset linux-debug

# Or just the shaders
cmake --build --preset linux-debug --target horde_shaders
```

**Commit the regenerated files in `shaders/compiled/`** with your shader change.
CI fails if they have drifted from `shaders/src/`.

Compile one shader by hand:

```sh
./tools/bin/shadercross shaders/src/sprite.vert.hlsl -o shaders/compiled/spirv/sprite.vert.spv
./tools/bin/shadercross shaders/src/sprite.vert.hlsl -o shaders/compiled/msl/sprite.vert.msl
```

Source language, output format and shader stage are all inferred from the
filenames (`.hlsl` in, `.spv`/`.msl`/`.dxil` out, `.vert`/`.frag`/`.comp` for the
stage).

DXIL is only emitted when building **on Windows** — it must be signed by
`dxil.dll`, which does not exist elsewhere. Everywhere else SDL_GPU uses Vulkan
and the SPIR-V.

---

## Assets

```sh
# Regenerate the placeholder sprite atlas
python3 tools/make_placeholder_atlas.py
```

Assets are copied next to the executable by the `horde_runtime_files` target on
every build.

---

## Formatting

```sh
# Format everything
clang-format -i $(git ls-files '*.cpp' '*.hpp')

# Check without modifying (what you want in a pre-commit hook)
clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.hpp')
```

---

## Debugging

```sh
# Which GPU backend and video driver got picked — printed at startup
./build/linux-debug/bin/horde

# Force a GPU backend
SDL_GPU_DRIVER=vulkan ./build/linux-debug/bin/horde

# Force X11 instead of native Wayland
SDL_VIDEO_DRIVER=x11 ./build/linux-debug/bin/horde

# More SDL logging
SDL_LOGGING=app=debug,gpu=debug ./build/linux-debug/bin/horde

# Vulkan validation layers (needs vulkan-validation-layers installed)
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ./build/linux-debug/bin/horde

# Under a debugger
gdb ./build/linux-debug/bin/horde
```

The GPU device is created with debug mode on in `AppConfig::gpuDebug`, so
SDL_GPU validation messages appear without extra setup.

---

## Dependencies

Versions are pinned in `cmake/Dependencies.cmake` as `HORDE_*_TAG` variables.
Each dependency uses a system package when one is present and fetches the pinned
tag otherwise.

```sh
# See what was picked — look for the "horde:" lines
cmake --preset linux-debug

# After changing a tag, wipe the cache
rm -rf build && cmake --preset linux-debug
```

Fedora / Nobara system packages:

```sh
sudo dnf install -y cmake ninja-build gcc-c++ git \
  SDL3-devel SDL3_image-devel glm-devel vulkan-loader-devel vulkan-headers
```
