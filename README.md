# horde

A C++20 game built on [SDL3](https://github.com/libsdl-org/SDL) and its GPU API.

Everything — the top-down world, the tech tree, menus and compute work — goes
through one renderer. `SDL_GPU` is a backend-agnostic layer over Vulkan, Metal
and D3D12, which means graphics *and* compute shaders on all three desktop
platforms without writing three renderers.

**You do not need to install anything except a compiler, CMake and Git.** If
SDL3 is available as a system package it is used; otherwise CMake fetches and
builds it. You do **not** need a shader toolchain — compiled shaders are
committed.

---

## Quick build & run

Once the one-time platform setup below is done, this configures, builds, and
runs `horde` in a single step:

```sh
./build.sh debug      # or: ./build.sh release
```

```powershell
.\build.ps1 debug     # or: .\build.ps1 release
```

`build.sh` (macOS/Linux) and `build.ps1` (Windows) detect the current platform
and pick the matching CMake preset automatically.

---

## Linux setup (Fedora / Nobara)

```sh
sudo dnf install -y cmake ninja-build gcc-c++ git \
  SDL3-devel SDL3_image-devel \
  glm-devel vulkan-loader-devel vulkan-headers
```

Optional, and only if you intend to edit shaders or debug the GPU backend:

```sh
sudo dnf install -y SDL3_ttf-devel glslc glslang spirv-tools vulkan-tools
```

<details>
<summary>Ubuntu / Debian</summary>

Ubuntu does not package SDL3 on older releases. If `libsdl3-dev` is
unavailable, install SDL3's own build dependencies instead and CMake will build
SDL3 from source:

```sh
sudo apt update
sudo apt install -y cmake ninja-build g++ git \
  libwayland-dev wayland-protocols libxkbcommon-dev libdecor-0-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev \
  libgl1-mesa-dev libegl1-mesa-dev libasound2-dev libpulse-dev libudev-dev
```
</details>

<details>
<summary>Building SDL3 from source on Fedora instead of installing SDL3-devel</summary>

If you skip `SDL3-devel`, CMake builds SDL3 from source and needs its X11/Wayland
development headers, which are a longer list than the ones above:

```sh
sudo dnf install -y libX11-devel libXext-devel libXrandr-devel libXcursor-devel \
  libXi-devel libXfixes-devel libXScrnSaver-devel libxkbcommon-devel \
  wayland-devel wayland-protocols-devel libdecor-devel \
  mesa-libGL-devel mesa-libEGL-devel alsa-lib-devel pipewire-devel systemd-devel
```

Installing `SDL3-devel` is much less work.
</details>

Clone with:

```sh
git clone git@github.com:edward-baker05/horde.git
cd horde
```

### Build and run

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
./build/linux-debug/bin/horde
```

Swap `linux-debug` for `linux-release` for an optimized build.

---

## Windows setup

### 1. Install Visual Studio 2022

Download the free **Community** edition from
<https://visualstudio.microsoft.com/downloads/>.

In the Visual Studio Installer, select the **"Desktop development with C++"**
workload. That single workload provides everything needed: the MSVC compiler,
the Windows SDK, CMake, and Ninja. Do not install CMake separately — Visual
Studio's bundled copy is used automatically.

### 2. Install Git

Download from <https://git-scm.com/download/win> and install with the defaults.

Git is required at *build* time, not just to clone the repo: CMake uses it to
download SDL3 and Dear ImGui. If `git --version` doesn't work in a fresh
terminal, Git isn't on your `PATH` — re-run the installer and choose "Git from
the command line and also from 3rd-party software".

### 3. Clone and open

```
git clone https://github.com/edward-baker05/horde.git
cd horde
```

Then **open the `horde` folder in Visual Studio** via `File > Open > Folder...`
(*not* `Open > Project`, and do not create a solution — this is a CMake
project).

Visual Studio detects `CMakeLists.txt` and `CMakePresets.json` and configures
automatically. The first configure downloads and compiles SDL3, which takes
several minutes; watch the CMake output pane and wait for it to finish.

### 4. Build and run

Pick the **Windows (Visual Studio 2022)** configuration in the toolbar, select
`horde.exe` as the startup item, then press <kbd>F5</kbd> to build and run.

<details>
<summary>Prefer the command line?</summary>

Open a **"Developer PowerShell for VS 2022"** from the Start menu (a normal
PowerShell window won't have the compiler on its `PATH`), then:

```
cmake --preset windows
cmake --build --preset windows-debug
.\build\windows\bin\Debug\horde.exe
```
</details>

By default Windows runs the **Vulkan** backend, because the D3D12 backend needs
signed DXIL shaders — see [Shaders](#shaders).

### Other Windows editors

- **VS Code** — install the [C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools)
  and [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)
  extensions, plus Visual Studio's "Desktop development with C++" workload for
  the compiler itself. CMake Tools reads `CMakePresets.json` and lists the
  presets directly.
- **CLion** — opens the folder and picks up the presets with no extra
  configuration.

---

## macOS setup

```sh
xcode-select --install
brew install cmake ninja git
```

`xcode-select --install` installs Apple's Command Line Tools, which provide the
Clang compiler. `brew` is [Homebrew](https://brew.sh) — install it first if you
don't already have it.

Optionally `brew install sdl3 sdl3_image glm` to skip building SDL3 from source.

```sh
git clone git@github.com:edward-baker05/horde.git
cd horde
cmake --preset macos-debug
cmake --build --preset macos-debug
./build/macos-debug/bin/horde
```

---

## Project layout

```
horde/
├── CMakeLists.txt      build definition — add new .cpp files here
├── CMakePresets.json   shared build configurations
├── cmake/
│   ├── Dependencies.cmake  system-package-or-fetch logic for each dependency
│   └── Shaders.cmake       shader compilation
├── build.sh            quick build & run script (macOS/Linux)
├── build.ps1           quick build & run script (Windows)
├── tools/
│   ├── build_shadercross.sh|.ps1   builds the shader compiler (only if editing shaders)
│   └── make_placeholder_atlas.py   regenerates the placeholder sprite atlas
├── assets/             copied next to the executable at build time
│   ├── fonts/          bundled fonts (see licensing note below)
│   └── textures/       sprite atlases
├── shaders/
│   ├── src/            HLSL sources — the only shader files you edit
│   └── compiled/       committed SPIR-V / MSL / DXIL, copied next to the executable
└── src/
    ├── main.cpp        entry point
    ├── app/App         window, GPU device, frame loop
    ├── core/Paths      asset path resolution
    ├── gfx/            GpuContext, ShaderLoader, Texture, SpriteBatch, Camera2D
    ├── scene/          Scene, SceneStack, MainMenu, Level, TechTree
    └── ui/ImGuiLayer   Dear ImGui setup
```

Builds go to `build/<preset-name>/`, and the executable to that directory's
`bin/`. On Windows (a multi-config generator) there's an extra level:
`bin/Debug/horde.exe`. The whole `build/` directory is gitignored and safe to
delete at any time.

Runtime files are found relative to the executable (`SDL_GetBasePath()`), not
the working directory, so the game runs correctly however it is launched.

The bundled font is [Liberation Sans](https://github.com/liberationfonts/liberation-fonts),
used in place of Arial. Arial itself is proprietary and can't be redistributed
here, but Liberation Sans is metrically compatible with it and licensed under
the SIL Open Font License — see `assets/fonts/LICENSE.txt`.

`assets/textures/atlas.png` is placeholder art generated by
`tools/make_placeholder_atlas.py`. Replace it with real art whenever; nothing in
the code depends on it being generated.

---

## Architecture

**One renderer for everything.** `gfx::SpriteBatch` draws instanced textured
quads with one draw call per texture run. There is no vertex buffer — the vertex
shader builds each quad from `SV_VertexID` and reads transforms out of a storage
buffer, so queueing a sprite is just appending to a vector.

**One camera for the world and the tech tree.** `gfx::Camera2D` handles pan,
zoom, and screen↔world conversion; `gfx::CameraController` maps drag-to-pan and
wheel-to-zoom-at-cursor onto it. The tech tree is a pannable, zoomable graph of
sprite icons, which is a world-space rendering problem rather than a widget
problem — so it uses exactly the same batch and camera as the level does.

**Dear ImGui is for tooling only.** Inspectors, timings, toggles. Game-facing UI
(menus, tech tree nodes, labels) is drawn as sprites. Keeping that line clear is
what stops the tech tree turning into a fight with a widget toolkit.

**Scenes are a stack.** `scene::SceneStack` lets a transient view sit on top of
what it interrupts. Transitions are queued and applied between frames, so a
scene can safely request one from inside its own update.

**Compute is ready but not used yet.** `shaders/src/gradient.comp.hlsl` is a
working reference compute shader: it is compiled and shipped like any other, and
`ShaderLoader::loadCompute()` plus the `Scene::compute()` hook are in place to
run it. Nothing calls it — writing the first real compute pass is yours. The
reference exists so the binding rules and the build path are already proven when
you do.

---

## Shaders

Shaders are authored **once, in HLSL**, in `shaders/src/`. SDL_GPU documents its
resource binding model in HLSL `register`/`space` terms, so HLSL is the least
friction even on Linux. Each file's name tells the compiler its stage:
`*.vert.hlsl`, `*.frag.hlsl`, `*.comp.hlsl`.

SDL_GPU needs a different binary per backend — SPIR-V for Vulkan, MSL for Metal,
DXIL for D3D12. Those live under `shaders/compiled/` and **are committed to the
repo**. They're small, deterministic artifacts, and committing them is what lets
a normal build on any platform need no shader toolchain at all.

At runtime `gfx::ShaderLoader` picks the right directory from
`SDL_GetGPUShaderFormats()`.

### Editing a shader

You need the `shadercross` CLI. Build it once:

```sh
./tools/build_shadercross.sh          # or .\tools\build_shadercross.ps1
```

This is slow — SDL_shadercross publishes no releases and has no packaged
dependencies, so it vendors and compiles DirectXShaderCompiler, an LLVM fork.
Expect a large clone and a long build. It is a one-time cost, and only shader
authors pay it.

Once `tools/bin/shadercross` exists, reconfigure the project. CMake picks it up
and recompiles `shaders/src/*.hlsl` as part of every build. **Commit the
regenerated files in `shaders/compiled/` alongside your shader change** — CI
fails the build if they've drifted.

### The DXIL caveat

DXIL must be signed by `dxil.dll`, which Microsoft ships only on Windows. DXIL
produced anywhere else is unsigned and D3D12 rejects it outside developer mode,
so `cmake/Shaders.cmake` only emits DXIL when building on Windows. Everywhere
else — and on Windows until someone regenerates DXIL there — SDL_GPU uses its
Vulkan backend and the SPIR-V we do ship. That is a fully supported
configuration, not a degraded one.

### Binding rules

SDL_GPU fixes which register space each resource kind lives in. Getting this
wrong produces validation errors or silently wrong reads:

| Stage | Textures / read-only storage | Samplers | Read-write storage | Uniform buffers |
| --- | --- | --- | --- | --- |
| Vertex | `t[n], space0` | `s[n], space0` | — | `b[n], space1` |
| Fragment | `t[n], space2` | `s[n], space2` | — | `b[n], space3` |
| Compute | `t[n], space0` | `s[n], space0` | `u[n], space1` | `b[n], space2` |

The resource counts passed to `ShaderLoader::loadGraphics` / `loadCompute` must
match what the HLSL actually binds — SDL_GPU cannot introspect the binary.

---

## Common tasks

**Add a source file** — list it in the `add_executable` call in
`CMakeLists.txt`.

**Add a shader** — drop it in `shaders/src/` with the right stage suffix, build
with `shadercross` available, and commit the artifacts.

**Draw text** — link SDL3_ttf and use its GPU text engine, which drops straight
into an SDL_GPU render pass:

```cmake
find_package(SDL3_ttf CONFIG REQUIRED)
target_link_libraries(horde PRIVATE SDL3_ttf::SDL3_ttf)
```

Then `TTF_CreateGPUTextEngine(device)` and `TTF_GetGPUTextDrawData()`. This is
what menu labels and tech-tree node names need; until then those are ImGui.

**Change a dependency version** — edit the `HORDE_*_TAG` variables at the top of
`cmake/Dependencies.cmake`, then delete `build/` and reconfigure.

**Force a GPU backend** — `SDL_GPU_DRIVER=vulkan` (or `metal`, `direct3d12`).

---

## Code style

Formatting is defined by `.clang-format` (4-space indent, opening braces on the
same line, 120-column limit) so that everyone's editor produces identical output
and diffs stay free of formatting noise.

Enable format-on-save — no extra setup is needed, since every editor below finds
`.clang-format` automatically:

- **Visual Studio** — works out of the box. `Tools > Options > Text Editor >
  C/C++ > Formatting` confirms ClangFormat is enabled.
- **VS Code** — the C/C++ extension includes clang-format. Set
  `"editor.formatOnSave": true`.
- **CLion** — prompts to enable ClangFormat when it detects the file; accept.

To format from the command line:

```sh
clang-format -i $(git ls-files '*.cpp' '*.hpp')
```

---

## Troubleshooting

**First configure fails to download SDL3 or ImGui** — check that `git --version`
works in the same terminal, and that a proxy or firewall isn't blocking
`github.com`.

**A build breaks after pulling changes** — delete the `build/` directory and
reconfigure. CMake caches aggressively and can hold stale paths.

**`SDL_CreateGPUDevice failed`** — no usable GPU backend. On Linux check
`vulkaninfo --summary` runs and that Vulkan drivers are installed
(`mesa-vulkan-drivers`, or the proprietary NVIDIA driver). `SDL_GPU_DRIVER` can
force a specific backend.

**`Could not read '.../shaders/spirv/...'`** — the compiled shaders weren't
copied next to the executable. Rebuild; the `horde_runtime_files` target does
the copy.

**Wayland desktops (Linux)** — SDL3 supports Wayland natively; the startup log
line `Video driver: wayland` confirms it. Set `SDL_VIDEO_DRIVER=x11` to force
XWayland if you hit a compositor bug.

---

## License

Source is dual licensed under Public Domain and MIT — see `LICENSE.md`, which
also lists the licences of the bundled and fetched third-party components.
