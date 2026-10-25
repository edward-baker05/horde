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

---

## Benchmarking physics

`tools/bench.py` builds the release config, then runs the game once per
physics system and unit count. Each run boots straight into a level, warms up,
times a fixed number of seconds and quits. Results go to
`bench_results/<timestamp>/` (git-ignored): `summary.md`, `summary.csv`,
one JSON per run in `runs/`, and the game's output in `logs/`.

```sh
python tools/bench.py                                  # every system x 1k, 10k, 100k
python tools/bench.py --physics gpu-grid,cpu-grid --units 1k,10k,100k,1m --repeats 3
python tools/bench.py --no-build --duration 5 --frames # reuse the build, keep per-frame CSVs
python tools/bench.py --physics gpu-collide --collision-iterations 8 --relaxation 0.8
python tools/bench.py --help
```

If `cmake` is not on `PATH`, the script looks for CLion's and Visual Studio's
bundled copies, or uses `HORDE_CMAKE`.

What the columns mean:

| Column | Measures |
| --- | --- |
| fps, frame ms, p99 ms | whole frames, start to start. Can't go above a display or driver frame cap |
| cpu sim ms | the scene's `update()`: CPU-side physics |
| gpu sim ms | the scene's `compute()` work on its own command buffer, waited on: GPU-side physics |
| sim ms | cpu + gpu sim: what the physics system costs per frame |
| overlap %, penetration | collision quality at the end of the run: share of units overlapping another, and mean overlap depth as a fraction of a unit's diameter |

Benchmarks run with vsync and GPU validation off, a fixed 1/60 s simulation
step and a seeded random spawn, so runs with the same settings are comparable.
If the summary says the frame rate looks capped, a driver limiter (NVIDIA frame
cap, G-Sync/Reflex) is still syncing frames: compare by *sim ms*, or lift the
cap.

The game takes the same options directly, for a single run or to play a given
setup:

```sh
horde --list-physics
horde --bench --physics gpu-grid --units 100000 --duration 10 --out result.json
horde --physics cpu-grid --units 5000     # boots straight into that level, playable
```

Units are discs one unit size (5) across. Above roughly 180k units the default
2000x2000 level physically can't hold them without overlap, so expect overlap %
to climb there whatever the system.

### Adding a physics system

GPU systems can reuse the stages `gpu-collide` is built from:
`sim::GpuSpatialGrid` (neighbour lookup) and `sim::GpuUnitCollisions`
(unit-unit separation). See `docs/adr/0003-swappable-staged-unit-physics.md`.

1. Implement `sim::PhysicsSystem` (`src/sim/PhysicsSystem.hpp`) in
   `src/sim/<Name>.cpp`. Draw through `gfx::UnitRenderer` so every system pays
   the same rendering cost. CPU systems can stream positions with
   `sim::UnitStreamer`, as `CpuGridPhysics` does.
2. Add the `.cpp` to `CMakeLists.txt`.
3. Add one line to the table in `src/sim/PhysicsRegistry.cpp`.

It then shows up in `--list-physics`, in `tools/bench.py --physics all`, and in
the level's debug panel, which can restart the level with any system and unit
count.
