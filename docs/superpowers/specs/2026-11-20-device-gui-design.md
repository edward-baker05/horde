# Device GUI — design


## Context / intent

horde is a tower defence game, but every screen today is a floating Dear ImGui debug window (`MainMenuScene.cpp`, `TechTreeScene.cpp`, `LevelScene.cpp:195+`, `EditorUi.cpp`). Player-facing controls (HP, exit damage) sit in the same panel as solver tuning. `Scene.hpp` already says game UI belongs outside ImGui, but no game UI layer exists, and there is no text rendering outside ImGui.

The goal is game UI that feels native and diegetic: **the player is a character holding devices.**
- **Rounds:** viewed as a live CCTV feed on a landscape **tablet**. Towers to place sit in a bottom **dock**, like a home bar of apps.
- **Main menu and pause:** the character's **phone**, portrait. Simple lists.
- **Tech tree:** the phone in landscape, styled as a shopping app. The tree-as-shop idea is unresolved and deferred.
- **Level editor:** the same tablet treatment as rounds.
- **Dev testing ground:** kept until release. It uses a real embedded UI, with no stylistic polish.

## Goals
- Device bezels are visibly drawn, never just implied. The background behind the devices is plain black for now.
- Transitions:
  - Main menu → round: the phone slides down and the tablet slides in from the side. Quitting reverses it.
  - Pause: the phone rises in front of the tablet, and the tablet is blurred behind it so the phone is the focal point.
  - Animations are basic slides for now. The user will polish them nearer release.
- CCTV post-processing (scanlines, noise, vignette, slight chromatic aberration) applies **only to the camera feed**. The tablet app's UI (dock, labels such as CAM/REC/clock, touch ripple, placement ghost) is drawn crisp on top. An optional faint glass glare goes over the whole device.
- All game-facing UI is built with RmlUi.

## Non-goals (this pass)
- Tech tree design. It gets a landscape-phone frame plus a placeholder screen only.
- Real tower placement. No towers exist in the code, so the dock holds placeholder tower apps.
- Polished animation, device art, backgrounds, settings menu content.
- Full editor migration. Only the tablet and dock come now, and the other panels stay ImGui (see below).

## Anti-goals
- UI that still reads as a debug tool or as ImGui reskinned.
- Post-processing that muddies the touch UI and breaks the touchscreen fiction.
- Runtime shader compilation. The repo rule is that compiled shaders are committed.

## Approaches considered
1. **RmlUi (HTML/CSS-like) inside device screens — chosen.** It provides layout, text, lists, transitions, data binding and form controls, which the editor, dev ground and future shop all need. Costs: RmlUi 6.3 plus FreeType are fetched dependencies.
2. Custom toolkit on SpriteBatch. Full control, but text, layout and every widget would be hand-built before anything looks good.
3. Restyled ImGui. Fastest, but stays immediate-mode and fights the native-app feel.

## Decided design
**Device shell (App-owned).** `App` owns a phone and a tablet. Each has:
- an offscreen **screen texture** in the swapchain format, so existing pipelines (`SpriteBatch`, `UnitRenderer`) work unchanged;
- a bezel sprite;
- an animated transform (position, scale, orientation for the phone);
- a blur amount.

Scenes render into a device screen rather than the swapchain. A final composite pass draws the black background, then each visible device: screen texture, then bezel, then glare. Blur is a separable pass over a device's screen texture before compositing. This replaces "only the top scene draws" (`App.cpp:404-433`). The paused round's tablet keeps rendering, or freezes its last frame, under the blurred phone.

**Scene changes.**
- Each scene declares which device it lives on and its screen layout.
- `SceneStack` lets a scene beneath the top keep rendering when the top is an overlay (pause).
- Input is mapped from window to device-screen coordinates before reaching scenes and RmlUi, so the editor and placement keep working inside the tablet.

**RmlUi integration (`src/ui/`).**
- Our own `Rml::RenderInterface` on the project's `ShaderLoader`, with new HLSL shaders compiled and committed like the others. This avoids the stock SDL_GPU backend, which compiles shaders at runtime via SDL_shadercross and lacks clip masks.
- An `Rml::SystemInterface` on SDL.
- FreeType font engine using the bundled Liberation Sans.
- One RmlUi context per device screen.
- Documents (`.rml`) and styles (`.rcss`) live in `assets/ui/`.

**Screens.**
- **Main menu (phone, portrait):** status bar plus a list: Play (level choice as a sub-list), Upgrades, Level Editor, Dev Ground, Quit.
- **Pause (phone over blurred tablet):** Resume, Restart, Quit to menu. Opened with Esc.
- **Round (tablet):**
  - Feed: the level and units, rendered to an intermediate texture, then the CCTV effect pass, then drawn into the tablet screen.
  - App overlay: CAM label, REC dot, clock, HP/status, and a bottom dock of placeholder tower apps.
  - Tapping the feed shows a crisp touch ripple.
- **Editor (tablet):** the level in the tablet screen (no CCTV effects). Tools (select, paint, polyline, field region, and so on) become dock apps. The Level, Inspector, Contents and Files panels stay as ImGui windows for now, and are rebuilt as tablet sheets in a later stage.
- **Dev ground:** reached from the main menu. The existing `LevelScene` tuning, presets and explosion controls, rebuilt as a plain RmlUi side panel, full-window with no device. ImGui is retired from game scenes. It stays for the editor's remaining panels and the frame-stats overlay.
- **Tech tree:** phone in landscape with a placeholder "shop" screen and a back button.

**Round camera (user decision).**
- For now the round has one static CCTV camera that fits the whole level, with no pan or zoom.
- Pinch/scroll zoom with pan, and several switchable cameras (CAM 1/2/3), are coming later. So the feed's view comes from a `FeedCamera` source, a list of camera views with an active index, rather than a hard-coded fit. The overlay's CAM label reads from it, and input handling has a slot for camera gestures that does nothing yet.

**Editor panels (user decision).** The tablet shrinks or shifts to leave a column on the black background. The four remaining ImGui panels dock into that column, beside the bezel and never over it.

**Calls made while writing this up (check these):**
- *One `LevelScene` with a mode (Player / Dev)* rather than two classes, sharing the simulation. Player mode puts the scene on the tablet with the CCTV feed. Dev mode is full-window with the plain RmlUi panel.
- *Where each entry point goes:*
  - Menu → Play: Player mode.
  - Menu → Dev Ground: Dev mode.
  - `--level` and preset loads: Dev mode.
  - `--bench`: Dev mode with the device shell bypassed entirely, so timings stay comparable with earlier runs.
  - Editor play-test: Player mode on the tablet. Quitting it returns to the editor.
- *Dev ground entry:* Menu → Dev Ground opens a phone sub-list of levels and presets. Picking one slides the phone away to the full-window dev ground. Its panel keeps the existing "restart with" controls for switching after that.
- *Esc and pause are generic to the device shell:*
  - In the editor, Esc first cancels an in-progress action, such as a polyline. With nothing in progress, it opens the phone pause (Resume / Quit to menu).
  - The tech tree uses Back or Esc to return.
  - Menu ↔ editor uses the same phone-down / tablet-in slides as a round.
- *Round input:* a tap on the feed makes a ripple. Picking a placeholder dock app shows a crisp ghost that follows the cursor. Tapping drops a ripple, places nothing, and clears the selection. Explosions and fling are dev-ground only.
- *Frame stats:* an ImGui overlay toggled with F3 in any scene. It is off by default in player scenes.
- *Scene lifetime and animation:* the menu scene stays alive under the round (Menu → Round → Pause on the stack). Each scene declares its device. The shell watches stack changes and drives the slides, so scenes never animate devices themselves.

**Round vs dev ground split.** The player round shows only player information. Restart-with options, solver settings, explosion and preset controls move to the dev ground.

## Staging
1. Shell and RmlUi foundation:
   - device textures and compositing
   - blur
   - slide animator
   - RmlUi render, system and font interfaces
   - input mapping
2. Phone main menu and pause, with transitions.
3. Round tablet: feed effect pass, overlay, placeholder dock.
4. Editor on tablet, with the tool dock.
5. Dev ground.
6. Tech tree placeholder.

## Left to the builder
- Device logical resolutions and DPI scaling.
- Whether RmlUi draws through SpriteBatch or its own pipeline.
- Splitting SpriteBatch upload/flush per device.
- How long blur and slides take.
- Bezel art, placeholder rectangles first.
- The exact CCTV shader parameters.
- Whether the paused tablet keeps rendering live or freezes.

## Verification
- `./build.sh debug` builds on Linux.
- CI (`.github/workflows/ci.yml`) stays green, including Windows and macOS shader formats committed for the new shaders.
- Walk through manually:
  - launch to phone menu
  - Play: phone slides down, tablet slides in
  - Esc: phone rises, tablet blurs
  - Resume
  - Quit to menu: transitions reversed
  - editor dock tools work, with clicks landing correctly inside the tablet
  - dev ground controls restart a level with new settings
- Feed effects are visible on the level but not on the dock or labels.
- The benchmark path (`--bench`) still runs headless and is unaffected.
