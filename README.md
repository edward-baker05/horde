# horde

A C++17 SFML 3 project, based on the [official SFML CMake template](https://github.com/SFML/cmake-sfml-project).

**You do not need to install SFML.** CMake downloads and builds SFML 3.1.0 from source
automatically the first time you configure the project. You only need a compiler, CMake,
and Git — plus, on Linux, SFML's own build dependencies.

---

## Windows setup

### 1. Install Visual Studio 2022

Download the free **Community** edition from <https://visualstudio.microsoft.com/downloads/>.

In the Visual Studio Installer, select the **"Desktop development with C++"** workload.
That single workload provides everything needed: the MSVC compiler, the Windows SDK,
CMake, and Ninja. Do not install CMake separately — Visual Studio's bundled copy is used
automatically.

### 2. Install Git

Download from <https://git-scm.com/download/win> and install with the defaults.

Git is required at *build* time, not just to clone the repo: CMake uses it to download
SFML. If `git --version` doesn't work in a fresh terminal, Git isn't on your `PATH` —
re-run the installer and choose "Git from the command line and also from 3rd-party software".

### 3. Clone and open

```
git clone https://github.com/edward-baker05/horde.git
cd horde
```

Then **open the `horde` folder in Visual Studio** via `File > Open > Folder...`
(*not* `Open > Project`, and do not create a solution — this is a CMake project).

Visual Studio detects `CMakeLists.txt` and `CMakePresets.json` and configures
automatically. The first configure downloads and compiles SFML, which takes several
minutes; watch the CMake output pane and wait for it to finish.

### 4. Build and run

Pick the **Windows (Visual Studio 2022)** configuration in the toolbar, select `horde.exe`
as the startup item, then press <kbd>F5</kbd> to build and run.

<details>
<summary>Prefer the command line?</summary>

Open a **"Developer PowerShell for VS 2022"** from the Start menu (a normal PowerShell
window won't have the compiler on its `PATH`), then:

```
cmake --preset windows
cmake --build --preset windows-debug
.\build\windows\bin\Debug\horde.exe
```
</details>

### Other Windows editors

- **VS Code** — install the [C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools)
  and [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)
  extensions, plus Visual Studio's "Desktop development with C++" workload for the
  compiler itself. CMake Tools reads `CMakePresets.json` and lists the presets directly.
- **CLion** — opens the folder and picks up the presets with no extra configuration.

---

## Linux setup (Fedora / Nobara)

```sh
sudo dnf install -y cmake ninja-build gcc-c++ make git \
  libX11-devel libXrandr-devel libXcursor-devel libXi-devel libXext-devel \
  mesa-libGL-devel freetype-devel systemd-devel \
  libogg-devel libvorbis-devel flac-devel \
  mbedtls-devel libssh2-devel
```

Unlike Windows, Linux needs SFML's dependencies as system packages, because SFML links
against the system's X11, OpenGL, and font libraries rather than bundling them.
`mbedtls-devel` and `libssh2-devel` are required by SFML's Network module, which is built
even though this project only links `SFML::Graphics`.

Do **not** install the `SFML-devel` package. Fedora ships SFML 2.6.2, whose API differs
substantially from the SFML 3 this project targets.

<details>
<summary>Ubuntu / Debian</summary>

```sh
sudo apt update
sudo apt install -y cmake ninja-build g++ git \
  libx11-dev libxrandr-dev libxcursor-dev libxi-dev libxext-dev \
  libgl1-mesa-dev libegl1-mesa-dev libfreetype-dev libudev-dev \
  libogg-dev libvorbis-dev libflac-dev \
  libmbedtls-dev libssh2-1-dev
```
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

## Project layout

```
horde/
├── CMakeLists.txt      build definition — add new .cpp files here
├── CMakePresets.json   shared build configurations
├── .clang-format       shared code style
└── src/
    └── main.cpp        entry point
```

Builds go to `build/<preset-name>/`, and the executable to that directory's `bin/`.
On Windows (a multi-config generator) there's an extra level: `bin/Debug/horde.exe`.
The whole `build/` directory is gitignored and safe to delete at any time.

## Common tasks

**Add a source file** — list it in the `add_executable` call in `CMakeLists.txt`:

```cmake
add_executable(horde src/main.cpp src/game.cpp)
```

**Use SFML audio or networking** — add the module to `target_link_libraries`:

```cmake
target_link_libraries(horde PRIVATE SFML::Graphics SFML::Audio)
```

`SFML::Graphics` already pulls in `SFML::Window` and `SFML::System`, so those never need
to be listed.

**Change the SFML version** — edit `GIT_TAG` in `CMakeLists.txt`, then delete `build/` and
reconfigure.

## Code style

Formatting is defined by `.clang-format` (4-space indent, opening braces on the same line,
120-column limit) so that everyone's editor produces identical output and diffs stay
free of formatting noise.

Enable format-on-save — no extra setup is needed, since every editor below finds
`.clang-format` automatically:

- **Visual Studio** — works out of the box. `Tools > Options > Text Editor > C/C++ >
  Formatting` confirms ClangFormat is enabled.
- **VS Code** — the C/C++ extension includes clang-format. Set `"editor.formatOnSave": true`.
- **CLion** — prompts to enable ClangFormat when it detects the file; accept.

To format from the command line:

```sh
clang-format -i src/*.cpp src/*.hpp
```

## Troubleshooting

**First configure fails to download SFML** — check that `git --version` works in the same
terminal, and that a proxy or firewall isn't blocking `github.com`.

**A build breaks after pulling changes** — delete the `build/` directory and reconfigure.
CMake caches aggressively and can hold stale paths.

**Wayland desktops (Linux)** — SFML 3.1 is X11-only and runs through XWayland. This works,
but is the likely cause of any oddities with cursor grabbing or per-monitor DPI scaling.
Run `echo $XDG_SESSION_TYPE` to see which session you're in.

## License

Template source is dual licensed under Public Domain and MIT — see `LICENSE.md`.
