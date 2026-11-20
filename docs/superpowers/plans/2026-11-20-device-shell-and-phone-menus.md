# Device Shell and Phone Menus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the ImGui main menu with an RmlUi phone, put the player round on a bezelled tablet, and add the phone pause over a blurred tablet, with slide transitions between them.

**Architecture:**
- An App-owned `ui::DeviceShell` owns a phone and a tablet. Each has an offscreen screen texture in the swapchain format, a placement tween and a blur tween.
- Each scene declares a `ScenePresentation` (which device it lives on, and whether it is an overlay). The shell derives each device's target placement from the scene stack and animates towards it.
- Each frame, App renders the topmost scene on each visible device into that device's texture, then RmlUi on top of it. It then composites the devices, with bezels and optional blur, onto a black swapchain. Scenes with no device (dev ground, editor, tech tree, `--bench`) render straight to the swapchain exactly as today.

**Tech Stack:** C++20, SDL3 GPU, RmlUi 6.3 (FreeType font engine), doctest for unit tests, HLSL compiled by the in-repo `tools/bin/shadercross`.

**Spec:** `docs/superpowers/specs/2026-11-20-device-gui-design.md`. This plan covers spec stages 1–2, plus the round's move onto the tablet and the Player/Dev entry-point split. The CCTV feed, dock, editor tablet, RmlUi dev panel and tech tree each get a later plan.

## Global Constraints

- Shaders are HLSL in `shaders/src/`. Compiled SPIR-V and MSL are committed in `shaders/compiled/`. DXIL for any new shader must be regenerated on Windows before merging to `main` (`Shaders.cmake` only emits DXIL there). Note this in the PR.
- No runtime shader compilation. Do not use RmlUi's stock `SDL_GPU` backend.
- Dependencies follow `cmake/Dependencies.cmake` policy: use the system package if found, otherwise FetchContent at a pinned tag. RmlUi tag `6.3`. FreeType: system or fetched `VER-2-13-3`. doctest `v2.4.11`.
- Devices: phone screen 390×844 logical px (portrait; landscape swaps the two) with a 14 px bezel. Tablet screen 1280×800 logical px with a 24 px bezel. The background behind the devices is black (`0,0,0,1`).
- Transitions are basic slides: ease-out cubic, 0.35 s. Blur fades in or out over 0.25 s. A hidden phone sits one window height below its centred position. A hidden tablet sits one window width to the right of its centred position.
- Bezels are placeholder solid rectangles (dark grey `0.08,0.08,0.09`) drawn with the atlas's solid cell. No device art yet.
- `--bench` output and timings must be unchanged: device-less scenes take the existing swapchain path.
- Commits are brief shorthand, at most 3 sentences, with **no** Co-Authored-By, "Generated with" or session-link lines (user's global rule).

## Review Focus

1. **Window resize or DPI change during a slide.** The device textures resize, scenes get `onResize` with the new screen size, and the in-progress slide carries on from its current fraction. Test: `fitDevice` at two window sizes keeps the device inside the window. `DeviceAnimator` placement fractions are window-independent (Task 2).
2. **Clicks outside a device's screen** (on the bezel or the black background). They must not reach the scene or RmlUi as clicks. Mouse motion is still delivered, clamped, so drags that leave the screen end cleanly. Test in Task 3: `mapMouseToScreen` returns `inside == false` with clamped coordinates.
3. **Esc pressed repeatedly or mid-transition.** It must never stack two pauses or pop the menu. Test in Task 2: `computeTargets` with a pause over the round keeps phone=Centered and tablet=Background. Task 6 gives PauseScene Esc → resume, and LevelScene pushes a pause only while it is the top scene.
4. **No saved levels, or exactly one.** Play goes straight into the single level, or the built-in one, as today. The Dev Ground list shows the built-in level when the levels folder is empty (Task 7 manual check).
5. **Quit to menu from the editor's play-test.** It must return to the editor, not the menu. The pause button reads "Back to editor" (Task 6 manual check).

---

### Task 1: Unit test harness, tweens and device layout

**Files:**
- Create: `src/ui/Tween.hpp`, `src/ui/DeviceLayout.hpp`, `src/ui/DeviceLayout.cpp`
- Create: `tests/test_main.cpp`, `tests/DeviceLayoutTests.cpp`, `tests/TweenTests.cpp`, `tests/CMakeLists.txt`
- Modify: `CMakeLists.txt` (add `src/ui/DeviceLayout.cpp`; `option(HORDE_BUILD_TESTS "" ON)`; `enable_testing()` (not `include(CTest)`, which would switch on RmlUi's own tests); `add_subdirectory(tests)`)
- Modify: `cmake/Dependencies.cmake` (doctest, only when `HORDE_BUILD_TESTS`)
- Modify: `CMakePresets.json` (a `testPresets` entry mirroring each build preset; Windows entries set `configuration`)
- Modify: `.github/workflows/ci.yml` (a `Test` step after Build: `ctest --preset ${{ matrix.platform.build_preset }} --output-on-failure`)

**Interfaces:**
- Produces:
  - `ui::Tween`: `float from, to, duration, elapsed`; `void retarget(float to, float duration)` (restarts from the current value); `void update(float dt)`; `float value() const` (ease-out cubic); `bool done() const`.
  - `float ui::easeOutCubic(float t)`.
  - `enum class ui::DeviceKind { None, Phone, Tablet }`, `enum class ui::Orientation { Portrait, Landscape }`.
  - `struct ui::DeviceSpec { glm::vec2 screenLogical; float bezelLogical; }`.
  - `ui::DeviceSpec ui::deviceSpec(DeviceKind, Orientation)`, which applies the global constants.
  - `struct ui::DeviceRect { glm::vec2 center; glm::vec2 screenSize; float scale; }`. `screenSize` is in window px and equals `screenLogical * scale`.
  - `ui::DeviceRect ui::fitDevice(const DeviceSpec&, glm::vec2 windowPx, float margin = 0.06f)` gives the largest scale at which screen plus 2×bezel fits inside the window minus `margin × window` on every side, centred.
  - `glm::ivec2 ui::screenPixelSize(const DeviceRect&)` (rounded, at least 1×1).
- The test target `horde_tests` compiles the pure `src/ui/*.cpp` files it needs directly and links doctest, glm and SDL3 headers.

- [ ] **Step 1: Write failing tests.**
  - `TweenTests.cpp`:
    - `easeOutCubic(0)==0`, `easeOutCubic(1)==1`, `easeOutCubic(0.5)≈0.875`.
    - A tween 0→1 over 0.35 s after `update(0.35f)` is `done()` with `value()==1`.
    - `retarget(0, 0.35)` at half-way starts from the current value and not from `to`.
  - `DeviceLayoutTests.cpp`:
    - Phone portrait in 1600×900: the device height `(844+28)*scale == 900*(1-0.12)` (±0.5).
    - The tablet in 1600×900 is height-limited too, and its centre is `(800,450)`.
    - `fitDevice` at 900×1600 keeps the tablet width-limited.
    - `deviceSpec(Phone, Landscape).screenLogical == {844,390}`.
- [ ] **Step 2: Run them to see them fail.** Run `cmake --preset linux-debug && cmake --build --preset linux-debug --target horde_tests`. Expected: compile failure (headers missing).
- [ ] **Step 3: Implement `Tween.hpp` (header-only) and `DeviceLayout.{hpp,cpp}`** to the interfaces above.
- [ ] **Step 4: Run the tests.** Run `ctest --preset linux-debug --output-on-failure`. Expected: all pass. The `horde` target still builds.
- [ ] **Step 5: Commit.** `git commit -m "doctest harness, tweens, device layout"`

### Task 2: Shell targets and device animator

**Files:**
- Create: `src/ui/ShellState.hpp`, `src/ui/ShellState.cpp`, `tests/ShellStateTests.cpp`
- Modify: `src/scene/Scene.hpp`: add `virtual ui::ScenePresentation presentation() const { return {}; }`, `virtual void onCovered() {}` and `virtual void onRevealed() {}`.

**Interfaces:**
- Consumes: `DeviceKind`, `Orientation`, `Tween` (Task 1).
- Produces:
  - `struct ui::ScenePresentation { DeviceKind device = DeviceKind::None; Orientation orientation = Orientation::Landscape; bool overlay = false; }`.
  - `enum class ui::Placement { Hidden, Centered, Background }`.
  - `struct ui::ShellTargets { Placement phone = Hidden; Placement tablet = Hidden; Orientation phoneOrientation = Portrait; }`.
  - `ui::ShellTargets ui::computeTargets(std::span<const ScenePresentation> stack)`, where `stack` runs bottom to top:
    - The top scene's device is Centered.
    - If the top scene is an overlay, the device of the nearest scene below it that is on a *different* device becomes Background.
    - Every other device is Hidden.
    - A top scene with `DeviceKind::None` hides both devices.
  - `class ui::DeviceAnimator { void setTarget(Placement); void update(float dt); float offscreen() const; /* 0 on, 1 hidden */ float blur() const; /* 0..1 */ bool visible() const; /* offscreen()<1 */ bool settled() const; }`. Background means offscreen 0 with blur 1. Tween times come from the global constraints.
- `Scene::presentation()`, `onCovered()` and `onRevealed()` are called by SceneStack (Task 5).

- [ ] **Step 1: Write failing tests** in `ShellStateTests.cpp`:
  - `[menu(Phone)]` → phone Centered, tablet Hidden.
  - `[menu, round(Tablet)]` → phone Hidden, tablet Centered.
  - `[menu, round, pause(Phone, overlay)]` → phone Centered, tablet Background.
  - `[menu, editor(None)]` → both Hidden.
  - `[menu, techtree(Phone, Landscape)]` → phone Centered with `phoneOrientation==Landscape`.
  - `DeviceAnimator`, starting Hidden, after `setTarget(Centered)` and `update(0.175f)` is `visible()` with `0<offscreen()<1`. After a further 0.2 s it is `settled()` with `offscreen()==0`.
  - `setTarget(Background)` reaches `blur()==1` after 0.25 s.
  - Retargeting Hidden mid-slide reverses from the current offset with no jump.
- [ ] **Step 2: Run them and see them fail.**
- [ ] **Step 3: Implement `ShellState.{hpp,cpp}`** and add the three Scene hooks.
- [ ] **Step 4: Run `ctest`.** Expected: pass.
- [ ] **Step 5: Commit.** `git commit -m "shell targets and device animator"`

### Task 3: RmlUi dependency, system interface and input mapping

**Files:**
- Modify: `cmake/Dependencies.cmake`:
  - FreeType: `find_package(Freetype QUIET)`. Otherwise `FetchContent_Declare(freetype … OVERRIDE_FIND_PACKAGE)` with zlib, png, harfbuzz, bzip2 and brotli disabled, plus `add_library(Freetype::Freetype ALIAS freetype)`.
  - RmlUi: `FetchContent_Declare(RmlUi GIT_TAG 6.3 …)` with `RMLUI_SAMPLES OFF`, `BUILD_TESTING OFF`, `RMLUI_FONT_ENGINE freetype`.
  - Link `RmlUi::RmlUi` into `horde` and `horde_tests`.
- Create: `src/ui/RmlSystem.hpp/.cpp`, `src/ui/RmlInput.hpp/.cpp`, `src/ui/ScreenInput.hpp/.cpp`, `tests/InputTests.cpp`

**Interfaces:**
- Consumes: `DeviceRect` (Task 1).
- Produces:
  - `class ui::RmlSystem : public Rml::SystemInterface`: `GetElapsedTime` from `SDL_GetTicksNS`; `LogMessage` to `SDL_Log` (warnings and errors at their own priority); clipboard through SDL.
  - `Rml::Input::KeyIdentifier ui::toRmlKey(SDL_Keycode)` covers letters, digits, arrows, Return, Escape, Backspace, Tab, Delete, Home, End, PageUp/Down, Space and F1–F12. Anything else maps to `KI_UNKNOWN`.
  - `int ui::toRmlModifiers(SDL_Keymod)`.
  - `bool ui::forwardToRml(Rml::Context&, const SDL_Event&)` feeds a *screen-space* event to the context. It returns true if RmlUi consumed it (the inverse of RmlUi's Process* return value).
  - `struct ui::ScreenPoint { glm::vec2 pixel; bool inside; }`.
  - `ui::ScreenPoint ui::mapMouseToScreen(const DeviceRect&, glm::vec2 offset, glm::vec2 windowPx)` converts a window pixel to screen-texture pixels (`(p - (center+offset) + screenSize/2)`). The result is clamped to the screen, with `inside` reporting whether it was in bounds.
  - `bool ui::remapMouseEvent(SDL_Event&, const DeviceRect&, glm::vec2 offset)` rewrites x/y on motion, button and wheel events. It returns false for button-down events outside the screen, and the caller drops those.
- Window coordinates: SDL mouse coordinates are in window points. Multiply by `SDL_GetWindowPixelDensity` before mapping, matching the pixel-sized device rects.

- [ ] **Step 1: Write failing tests** in `InputTests.cpp`:
  - `toRmlKey(SDLK_ESCAPE)==KI_ESCAPE`, `toRmlKey(SDLK_A)==KI_A`, `toRmlKey(SDLK_KP_0)==KI_UNKNOWN`.
  - With a tablet rect centred at (800,450), screenSize (1000,625) and offset 0:
    - the window centre maps to (500,312.5) with `inside`;
    - window (100,100) maps to `inside==false`, clamped to (0,0);
    - with offset (1600,0), window (800,450) is outside.
  - `remapMouseEvent` on a button-down at (100,100) returns false. On a motion event at (100,100) it returns true with clamped coordinates.
- [ ] **Step 2: Run them and see them fail.**
- [ ] **Step 3: Implement the files and add the CMake changes.** Run a full configure to prove FreeType and RmlUi resolve.
- [ ] **Step 4: Run `ctest`.** Expected: pass. `horde` builds and links RmlUi.
- [ ] **Step 5: Commit.** `git commit -m "rmlui dep, system interface, input mapping"`

### Task 4: RmlUi renderer and the main menu document

**Files:**
- Create: `shaders/src/rml.vert.hlsl`, `shaders/src/rml.frag.hlsl` and their compiled `spirv`/`msl` outputs.
- Create: `src/ui/RmlRenderer.hpp/.cpp` and `src/ui/RmlLayer.hpp/.cpp`.
- Create: `assets/ui/phone.rcss`, `assets/ui/main_menu.rml`.
- Modify: `src/app/App.{hpp,cpp}` (own `RmlLayer`; route events; record and draw), `src/scene/Scene.hpp` (`Services::ui`), `src/scene/MainMenuScene.{hpp,cpp}`.

**Interfaces:**
- Consumes: `RmlSystem`, `forwardToRml` (Task 3); `gfx::ShaderLoader::loadGraphics`.
- Produces:
  - `class ui::RmlRenderer : public Rml::RenderInterface`, implementing the RmlUi 6 core set: `CompileGeometry`, `RenderGeometry`, `ReleaseGeometry`, `LoadTexture` (SDL_image, premultiplied on load), `GenerateTexture`, `ReleaseTexture`, `EnableScissorRegion`, `SetScissorRegion`, `SetTransform`. Clip masks, layers and filters stay unimplemented in this plan.
    - **Record/replay.** SDL_GPU forbids uploads inside a render pass, but RmlUi compiles geometry and glyph textures during `Context::Render()`. So:
      - `void beginRecord(SDL_GPUCommandBuffer*, glm::ivec2 targetSize)` is called before any pass. `Context::Render()` then runs, uploading through copy passes and appending draw commands to a list.
      - `void draw(SDL_GPURenderPass*, SDL_GPUCommandBuffer*)` replays the list inside the target's pass.
    - One pipeline for the target format: premultiplied blending (`ONE`, `ONE_MINUS_SRC_ALPHA`). Untextured geometry binds a 1×1 white texture.
    - Vertex layout: `Rml::Vertex` (float2 position, ubyte4-normalised colour, float2 uv).
    - Uniforms: an orthographic y-down projection for `targetSize` multiplied by the current transform, plus the translation.
  - `class ui::RmlLayer`:
    - `bool init(gfx::GpuContext&, const gfx::ShaderLoader&)` installs the interfaces, runs `Rml::Initialise()` and loads `fonts/LiberationSans-Regular.ttf`.
    - `void shutdown()`.
    - `Rml::Context* context(DeviceKind)`, with one context per device (`None` is the full window).
    - `void setContextSize(DeviceKind, glm::ivec2 pixels, float dpRatio)`.
    - `void record(DeviceKind, SDL_GPUCommandBuffer*)`, then `void draw(DeviceKind, SDL_GPURenderPass*, SDL_GPUCommandBuffer*)`.
    - `bool handleEvent(DeviceKind, const SDL_Event&)`.
  - `Services::ui` is a `ui::RmlLayer*`.
- `MainMenuScene`:
  - Loads `ui/main_menu.rml` into a data model `"menu"` with:
    - `levels` (display names), `presets` (names), `page` (`"root"|"play"|"dev"`);
    - event callbacks `open(page)`, `back`, `play(i)`, `dev_level(i)`, `dev_preset(i)`, `upgrades`, `editor`, `quit`.
  - Root list: Play, Upgrades, Level Editor, Dev Ground, Quit.
  - Play with zero or one levels starts immediately, as today (`MainMenuScene.cpp:44-50`).
  - The Dev Ground page lists the built-in level, then every level, then presets. Picking one starts a Dev-mode level (mode comes in Task 7; until then it pushes `LevelScene` as today).
  - Quit pushes `SDL_EVENT_QUIT`.
  - Remove `MainMenuScene::debugUi`. The document closes in `onExit`, hides in `onCovered` and shows in `onRevealed`.
  - In this task the menu renders on the `None` context, full-window. Task 5 moves it to the phone.
- `phone.rcss` styles:
  - a status bar (clock from `SDL_GetCurrentTime`, refreshed each second);
  - large left-aligned list rows (sizes in `dp`), with a hover/active highlight and a 0.15 s background transition;
  - a header with a back chevron on sub-pages;
  - a dark theme (near-black background, off-white text, one accent colour).

- [ ] **Step 1: Write the shaders and build.** The build regenerates `shaders/compiled/{spirv,msl}/rml.*` through `tools/bin/shadercross`. Expected: new files under `shaders/compiled/`.
- [ ] **Step 2: Implement `RmlRenderer` and `RmlLayer`.** Wire App:
  - init after ImGui;
  - `RmlLayer::handleEvent(None, e)` after ImGui and before the scene;
  - `context->Update()` in `App::update`;
  - `record` before the swapchain pass and `draw` after the scene and before ImGui.
- [ ] **Step 3: Write `main_menu.rml`/`phone.rcss` and convert `MainMenuScene`.**
- [ ] **Step 4: Verify manually.** Run `./build.sh debug`.
  - The menu renders with crisp text and hover highlights.
  - Play, Upgrades, Level Editor, Dev Ground with its sub-list, back, and Quit all work.
  - The Vulkan validation log is clean (`--gpu-debug` is on by default in debug).
- [ ] **Step 5: Commit.** `git commit -m "rmlui renderer, main menu in rml"`

### Task 5: Device shell: screens, compositing, slides and input routing

**Files:**
- Create: `src/gfx/RenderTarget.hpp/.cpp`, `src/ui/DeviceShell.hpp/.cpp`
- Modify: `src/scene/SceneStack.{hpp,cpp}`, `src/app/App.{hpp,cpp}`, `src/scene/Scene.hpp` (`Services::shell`), `src/scene/MainMenuScene.{hpp,cpp}`, `src/scene/LevelScene.{hpp,cpp}`, `src/scene/EditorScene.cpp`, `src/scene/TechTreeScene.cpp`

**Interfaces:**
- Consumes: Tasks 1–4.
- Produces:
  - `class gfx::RenderTarget`:
    - `bool resize(SDL_GPUDevice*, SDL_GPUTextureFormat, glm::ivec2)`, a no-op when the size is unchanged. Usage is `COLOR_TARGET | SAMPLER`.
    - `SDL_GPUTexture* texture() const`, `glm::ivec2 size() const`.
  - `class ui::DeviceShell`:
    - `bool init(gfx::GpuContext&, const gfx::ShaderLoader&)`.
    - `void layout(glm::vec2 windowPx)`.
    - `void setTargets(const ShellTargets&)`, `void update(float dt)`.
    - `bool visible(DeviceKind) const`, `glm::ivec2 screenSize(DeviceKind) const`.
    - `RenderTarget& screen(DeviceKind)`, `gfx::SpriteBatch& batch(DeviceKind)`, with one batch per device so uploads don't collide.
    - `bool remapEvent(DeviceKind, SDL_Event&) const`.
    - `void composite(gfx::SpriteBatch& compositeBatch, SDL_GPUTexture* atlas, glm::vec2 windowPx)`, which queues each visible device back to front (Background before Centered). For each device: the bezel rect, then the screen texture quad at `center + slide offset`.
  - `SceneStack`:
    - `std::span<const std::unique_ptr<Scene>> scenes() const`.
    - `Scene* topmostOn(DeviceKind) const`.
    - Calls `onCovered()` on the previous top when pushing, and `onRevealed()` on the new top after a pop.
    - After every successful `onEnter`, and on resize, calls `onResize` with the scene's viewport: its device's `screenSize`, or the window size for `None`.
- App frame order (all uploads before any pass):
  1. `shell.setTargets(computeTargets(presentations))`, then `shell.update(dt)`.
  2. Only the top scene gets `update` and `compute`. Covered scenes are frozen.
  3. For each visible device with a `topmostOn` scene: `batch(device).begin()`, `scene->render`, `upload`; `rml.record(device, …)`.
  4. Compositing: the composite batch (SpriteBatch with an orthographic window-pixel camera) is filled by `shell.composite`, then uploaded. If the top scene is `None`, the main batch is used as today.
  5. Each device pass, on `screen(device)`:
     - Clear to the scene background (`0.06,0.07,0.09`).
     - `flush` with that scene's camera (viewport = screen size).
     - `scene->renderPass`.
     - `rml.draw(device)`.
  6. Swapchain pass:
     - Clear to black.
     - If the top scene is `None`, do today's flush, then `renderPass`, then RmlUi `None`.
     - The composite batch flush.
     - ImGui.
- Input: ImGui first.
  - Then, for the top scene's device, `shell.remapEvent` (button-downs outside the screen are dropped).
  - Then `rml.handleEvent(device, e)`.
  - Then `scene->handleEvent`.
  - `None` scenes skip the remap.
- `UnitRenderer` and the scene `SpriteBatch` pipelines already target the swapchain format, so they need no change.
- Presentations:
  - `MainMenuScene` → `{Phone, Portrait}`, with the document moved to the phone context.
  - `LevelScene` → `{Tablet}`. Task 7 adds Dev → `None`.
  - Editor and tech tree → `{None}` (later plans).
- Scenes stop reading `SDL_GetWindowSizeInPixels` in `onEnter` for their camera viewport. The stack's post-enter `onResize` supplies it, so `LevelScene::fitCameraToLevel` fits the tablet screen.
- The DPI ratio passed to RmlUi contexts is `DeviceRect::scale`.

- [ ] **Step 1: Implement `RenderTarget`, `DeviceShell` and the SceneStack additions.** Then restructure `App::render` and `App::pumpEvents` to the order above.
- [ ] **Step 2: Build and run `ctest`.** Expected: still green.
- [ ] **Step 3: Verify manually.**
  - At launch the phone is centred with its bezel on black.
  - Play: the phone slides down and the tablet slides in from the right while the level runs on it.
  - Resizing the window mid-slide re-fits both devices without a jump.
  - Clicking the bezel does nothing.
  - Level Editor and Upgrades: the phone slides away and the full-window scene shows. Their Back buttons slide the phone back.
- [ ] **Step 4: Verify that bench is unchanged.** Run `./build/linux-release/bin/horde --bench` (release build) before and after, using the same arguments as a previous `tools/bench.py` run. Expected: it runs and quits with no devices drawn, and frame timings are within noise of the pre-change run.
- [ ] **Step 5: Commit.** `git commit -m "device shell, phone and tablet compositing, slides"`

### Task 6: Blur and the pause phone

**Files:**
- Create: `shaders/src/fullscreen.vert.hlsl`, `shaders/src/blur.frag.hlsl` and their compiled outputs; `src/gfx/Blur.hpp/.cpp`; `src/scene/PauseScene.hpp/.cpp`; `assets/ui/pause.rml`
- Modify: `src/ui/DeviceShell.cpp`, `src/app/App.cpp`, `src/scene/LevelScene.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `DeviceAnimator::blur()`, `DeviceShell::screen` (Task 5).
- Produces:
  - `class gfx::Blur`:
    - `bool init(SDL_GPUDevice*, const ShaderLoader&, SDL_GPUTextureFormat)`.
    - `SDL_GPUTexture* apply(SDL_GPUCommandBuffer*, SDL_GPUTexture* source, glm::ivec2 size)` runs a separable 9-tap gaussian, horizontal then vertical, at half resolution into two internal `RenderTarget`s. It returns the blurred texture.
    - The fragment uniform is `{float2 direction; float2 texelSize;}`.
  - `DeviceShell::composite`: for a device with `blur() > 0`, it draws the sharp screen, then the blurred texture over it with alpha `blur()`. Both are tinted by `1 - 0.35*blur()`. App runs `Blur::apply` after the device passes and before the swapchain pass.
  - `class scene::PauseScene : public Scene`:
    - `PauseScene(std::function<void()> onRestart, bool returnsToEditor)`.
    - `presentation() == {Phone, Portrait, overlay=true}`.
    - The document `pause.rml` has the buttons Resume, Restart, and "Quit to menu" or "Back to editor".
    - Resume and Esc → `pop()`.
    - Restart → `pop()` then `onRestart()`.
    - Quit → `pop(); pop();` (pause, then level).
    - It uses `camera()` but renders no sprites.
  - `LevelScene` (Player): Esc while top → `push(PauseScene(...))`.
    - `onRestart` → `scenes->replace(make_unique<LevelScene>(m_options))`.
    - `returnsToEditor = m_options.level.has_value()`.
- The covered round is frozen. The tablet keeps drawing its last state, blurred.

- [ ] **Step 1: Write the blur shaders.** Build them and confirm the compiled outputs appear.
- [ ] **Step 2: Implement `Blur`, the composite blending, `PauseScene` and `pause.rml`** (it reuses `phone.rcss`).
- [ ] **Step 3: Verify manually.**
  - In a round, Esc: the phone rises from below while the tablet blurs and dims over 0.25 s, and the units freeze.
  - Resume, or Esc again, reverses this.
  - Spamming Esc never stacks pauses.
  - Restart reloads the level.
  - Quit to menu: the phone stays, the tablet slides out right, and the menu list shows.
  - From the editor's play-test: the pause shows "Back to editor", and pressing it returns to the editor.
- [ ] **Step 4: Commit.** `git commit -m "blur pass and pause phone"`

### Task 7: Player and Dev entry points, plus the F3 frame-stats overlay

**Files:**
- Modify: `src/scene/LevelScene.{hpp,cpp}`, `src/scene/MainMenuScene.cpp`, `src/scene/EditorScene.cpp`, `src/app/CommandLine.cpp`, `src/app/App.{hpp,cpp}`
- Test: `tests/ShellStateTests.cpp` (one case: a Dev-mode level hides both devices)

**Interfaces:**
- Produces:
  - `enum class scene::LevelMode { Player, Dev }`, added as `LevelOptions::mode` with default `Player`.
  - `LevelScene::presentation()` is `{Tablet}` for Player and `{None}` for Dev.
  - Player mode:
    - no ImGui panel;
    - no camera pan or zoom: a fit-to-level static view;
    - explosion and fling disabled;
    - Esc opens the pause.
  - Dev mode keeps today's ImGui panel and controls unchanged. Its Back button pops to the menu. The RmlUi dev panel comes in a later plan.
  - Feed camera seam (from the spec): `struct scene::FeedCamera { std::vector<gfx::Camera2D> views; std::size_t active = 0; }`. Player mode builds one fitted view. It applies the active view's centre and zoom to `m_camera`, and its camera-gesture handler is an empty `handleCameraGesture(const SDL_Event&)` that returns false.
  - App: F3 toggles `m_showFrameStats`. When it is on, the existing `frameCostUi` content (moved from `LevelScene` into a free function `ui::drawFrameStats(const FrameStats&)` in `src/ui/FrameStatsOverlay.{hpp,cpp}`) is drawn as a small ImGui overlay in any scene. It defaults to on for `None` scenes and off for device scenes.
- Entry points:
  - Menu → Play: `Player`.
  - Menu → Dev Ground: `Dev`.
  - `--level`, presets and `--bench`: `Dev`, set in `CommandLine.cpp` and in the preset path.
  - Editor play-test (`EditorScene.cpp:705`): `Player`.

- [ ] **Step 1: Write the failing test** `[menu, level(Dev→None)]` → both Hidden, by giving the presentation helper `ui::ScenePresentation levelPresentation(LevelMode)` in `LevelScene.hpp`. Run it and see it fail.
- [ ] **Step 2: Implement the mode split, `FeedCamera`, the entry points and the F3 overlay.** Run `ctest`: pass.
- [ ] **Step 3: Verify manually.**
  - Play: the tablet is static and can't be panned.
  - F3 shows the frame stats.
  - Dev Ground → a preset: full window with the old panel, and Back returns to the phone.
  - `--level <file>` and `--bench` boot full-window.
  - With no levels saved, Play starts the built-in level and Dev Ground lists only "Built-in".
- [ ] **Step 4: Commit.** `git commit -m "player and dev level modes, f3 stats overlay"`
