# Level editor — design

**Date:** 2026-08-19
**Status:** approved, ready for implementation planning

A graphical level editor reachable from the main menu, alongside a level file
format and a shared level data model that the game itself will consume once
enemy and tower logic exist.

Vocabulary used throughout this document is defined in `CONTEXT.md` at the repo
root. Two decisions taken here are recorded as ADRs: `docs/adr/0001` (shapes as
textured quads) and `docs/adr/0002` (edge-relative markers).

---

## Scope

**In:** placing, moving, resizing, rotating and recolouring walls of four kinds
(rectangle, isoceles triangle, circle, polyline); placing and stretching spawn
and exit markers; toggleable grid and rotation snapping; undo/redo; loading and
saving levels as JSON; a menu entry point; and rendering a loaded level in
`LevelScene`.

**Out, deliberately:** collision, pathfinding, enemy routing and tower placement
(the game logic this data will eventually feed) are not part of this work. Also
excluded, with the data model leaving room for each: per-vertex polyline
editing, native OS file dialogs, wall reordering, wall translucency, and
spawn-to-exit pairing. There are no automated tests — see "Testing" below.

---

## 1. Data model

Lives in `src/logic/Level.hpp`, next to `UnitData`, because a level is game
data rather than editor data. The game must be able to load a level without
depending on any editor code.

A wall is a shared transform plus a kind-specific payload, held in a
`std::variant` rather than a struct with fields that do not apply to every kind:

```cpp
struct Rgb { std::uint8_t r, g, b; };

struct RectangleShape { glm::vec2 halfExtents; };
struct TriangleShape  { glm::vec2 halfExtents; };   // isoceles, apex toward -y
struct CircleShape    { float radius; };
struct PolylineShape  { std::vector<glm::vec2> points; float thickness; };

struct Wall {
    glm::vec2 center{};
    float rotation = 0.0f;                          // radians in memory
    Rgb color{};
    std::variant<RectangleShape, TriangleShape, CircleShape, PolylineShape> shape;
};

enum class Edge { North, South, East, West };
enum class MarkerKind { Spawn, Exit };

struct Marker { MarkerKind kind; Edge edge; float offset; float length; };

struct Level {
    glm::vec2 size{600.0f, 400.0f};                 // bounds are (0,0)..(size)
    Rgb backgroundColor{};
    std::vector<Wall> walls;                        // later index draws on top
    std::vector<Marker> markers;                    // >=1 spawn, >=1 exit, always
};
```

The variant is what keeps handle enumeration and future per-vertex editing
tractable: "which handles does this wall have" is a visitor, so adding vertex
handles later touches one `PolylineShape` case rather than every branch of a
switch.

Polyline points are stored in **local space** relative to `center`, so
whole-polyline move and rotate behave identically to every other wall kind.

A circle carries a `rotation` field it ignores. This is a deliberate wart:
special-casing circles out of the shared transform would cost more, in branching
spread across the editor, than one unused float costs.

**Coordinates.** The level origin is its top-left corner, with bounds running
`(0,0)` to `(size)`. This matches how `LevelScene` already draws its rectangle
and `UnitManager`'s existing `worldOrigin = {0,0}`, so no coordinate translation
is needed when the game starts consuming levels.

`Camera2D::viewProjection` flips y (`Camera2D.cpp:47-48`), so **world +y points
down**, matching screen coordinates. Two consequences the implementation must
respect: an isoceles triangle's apex points toward **-y** so that it reads as
apex-up on screen, and a rotation that `Sprite`'s comment calls
counter-clockwise appears **clockwise** on screen. The rotate handle and the
inspector's angle field must agree with what the user sees, not with the sign
convention in the shader.

---

## 2. File format

Levels are JSON at `assets/levels/*.level.json`. That directory is copied next
to the executable by the existing `horde_runtime_files` step, so saved levels
ship with the build automatically.

JSON was chosen over a binary or hand-rolled text format because levels are
hand-editable and diff readably in git, and because tolerance of unknown keys
gives forward compatibility for free as the schema grows to meet the game.

Rotations are stored in **degrees** and colours as **0-255 integers**, both for
the benefit of anyone hand-editing a file; memory holds radians and the
conversion happens at the boundary.

```json
{
  "version": 1,
  "size": [600, 400],
  "backgroundColor": [30, 34, 40],
  "walls": [
    { "kind": "rectangle", "center": [100, 120], "rotation": 0,  "color": [200, 60, 60], "halfExtents": [40, 15] },
    { "kind": "triangle",  "center": [260, 200], "rotation": 18, "color": [90, 140, 90], "halfExtents": [30, 40] },
    { "kind": "circle",    "center": [400, 300],                 "color": [80, 90, 200], "radius": 25 },
    { "kind": "polyline",  "center": [500, 100], "rotation": 0,  "color": [180, 180, 60],
      "thickness": 6, "points": [[0, 0], [50, 0], [50, 40]] }
  ],
  "markers": [
    { "kind": "spawn", "edge": "west", "offset": 150, "length": 100 },
    { "kind": "exit",  "edge": "east", "offset": 150, "length": 100 }
  ]
}
```

`version` exists so a future schema change can migrate rather than fail. A file
whose version is unrecognised fails to load with an explanatory error.

Circles omit `rotation` when written, since it is meaningless for them, and any
wall missing the key reads back as `0`. Every other key is required, and its
absence is a load error rather than a silently defaulted value — a level file
missing a wall's `center` is a corrupt file, not a file with a wall at the
origin.

**API**, in `src/logic/LevelIO.hpp`:

```cpp
std::optional<Level> loadLevel(const std::filesystem::path& path, std::string* error);
bool saveLevel(const Level& level, const std::filesystem::path& path, std::string* error);
```

No exceptions escape these functions — nothing else in this codebase uses them.
`nlohmann/json`'s parse errors are caught at the boundary and converted to the
error string.

**Dependency.** `nlohmann/json` is added to `cmake/Dependencies.cmake` under the
established policy: `find_package` first (Fedora packages it as `json-devel`),
`FetchContent` of a pinned `HORDE_JSON_TAG` otherwise. It is header-only, so the
fetch path costs configure time but no build time.

---

## 3. Validation

`src/logic/LevelValidation.hpp` exposes `std::vector<Problem> validate(const
Level&)`, where a `Problem` carries the offending wall's index and a
human-readable message. Two rules:

- **Out of bounds** — the wall's rotated axis-aligned bounding box is not fully
  inside `(0,0)..(size)`.
- **Degenerate** — non-positive extents or radius, or a polyline with fewer than
  two points.

Saving is refused while any problem exists. The editor's validation panel lists
every problem and click-selects the offending wall.

Validation lives in `logic/` rather than the editor so the game can assert the
same invariants when it loads a level.

Marker constraints are absent from this list by design. Overlap is prevented
during dragging rather than reported afterwards, and the always-present minimum
of one spawn and one exit means the "no markers" state cannot be reached. Both
are invariants maintained by construction, not conditions to be checked.

---

## 4. Rendering

`src/gfx/LevelRenderer.hpp` exposes a single
`renderLevel(const Level&, SpriteBatch&, SDL_GPUTexture* atlas)`, used by **both**
the editor and `LevelScene`, so the two views of a level cannot drift apart.

**Approach: textured quads from the atlas** — see `docs/adr/0001`. Circles and
triangles are alpha-masked atlas cells tinted by `Sprite::color`; polyline
segments are thin rotated quads, the technique `TechTreeScene` already uses for
its graph edges. No new pipeline, no shader changes, no regenerated shader
binaries.

**The atlas grows from 2x2 cells at 64px to 4x4 cells at 128px (512x512)**,
regenerated by `tools/make_placeholder_atlas.py` with supersampled alpha so
edges are smooth rather than stair-stepped at high zoom. Existing cells keep
their grid coordinates and a new isoceles-triangle cell is added.

This ripples outside the feature: the six existing `gfx::atlasCell(c, r, 2, 2)`
calls in `LevelScene.cpp` and `TechTreeScene.cpp` must become `(c, r, 4, 4)`.
The change is mechanical but must not be missed, or those scenes will sample the
wrong cells.

**Centre-origin drawing.** `gfx::Sprite::position` is the quad's **top-left**
corner and rotation happens **about that corner**, but every shape in a level is
defined by its centre. Each draw therefore needs
`topLeft = center - R(theta) * halfExtents`. A single `drawCentered(...)` helper
is added to `gfx` and used everywhere, replacing the ad-hoc version currently
inlined at `TechTreeScene.cpp:35`.

Every element samples the same atlas, so a whole level renders as one draw call.

---

## 5. Editor structure

The editor is split into small, independently understandable units rather than
one large scene file:

| File | Responsibility |
|---|---|
| `src/scene/EditorScene.{hpp,cpp}` | Scene shell: camera, input routing, per-frame orchestration |
| `src/editor/EditorState.hpp` | The `Level`, current selection, active tool, snap settings, undo stack |
| `src/editor/Tools.{hpp,cpp}` | Tool state machine: select / rectangle / triangle / circle / polyline / spawn / exit |
| `src/editor/HitTest.{hpp,cpp}` | Point-in-wall tests, performed in each kind's local space |
| `src/editor/Handles.{hpp,cpp}` | Which handles a wall has, where they sit, what dragging one does |
| `src/editor/UndoStack.hpp` | Whole-`Level` snapshots |
| `src/editor/LevelFiles.{hpp,cpp}` | Enumerating `assets/levels/`, load and save plumbing |
| `src/editor/EditorUi.{hpp,cpp}` | Every ImGui panel: toolbar, inspector, wall list, validation, file browser |

ImGui is the right tool here despite the project's "ImGui is for tooling only"
rule: a level editor *is* tooling. Game-facing UI remains sprite-drawn.

### Interaction

**Select mode.** Hovering highlights the wall under the cursor. Clicking selects
it. Dragging its body moves it. The wall list panel offers unambiguous selection
when shapes overlap.

**Manipulation.** A selected wall shows corner resize handles on its bounding
box plus a rotate handle offset outward, *and* is editable through an inspector
panel with exact numeric centre, size, rotation and RGB fields. Handles give
feel; the inspector gives precision. Handle hit-testing transforms the cursor
into the wall's unrotated local space, which is what keeps it correct at
arbitrary angles.

**Placement.** Click-drag sweeps out the shape's bounding box, so placement and
initial sizing are one gesture. A translucent preview follows the cursor before
the drag commits. The tool stays armed after placing so runs of walls can be
placed without re-selecting; Escape returns to select mode.

**Polylines.** Click to add points. Enter or double-click finishes the line;
Escape cancels the whole in-progress line. Thickness is a per-polyline inspector
value. Once created, a polyline moves and rotates as a whole. Per-vertex editing
is future work the variant-based model already accommodates.

**Markers.** A marker has two handles, one per end, and dragging is clamped both
to its edge and against its neighbours, so overlap cannot occur. Markers are
drawn inward from the edge at a fixed global thickness of 12 world units. Spawns are green and
exits red; these colours are not editable, since markers are placeholders for a
future integration rather than authored art. A new level begins with one spawn
centred on the west edge and one exit centred on the east, and the last spawn
and last exit cannot be deleted.

**Snapping.** Two independent toggles, one for position and one for rotation,
because wanting axis-aligned angles while placing freely (or the reverse) is
routine. Both increments are user-settable, defaulting to 10 world units and
15 degrees, and **both default to off** — off-grid placement is the norm and
snapping is the opt-in aid. Position snapping constrains a wall's centre, the
one point every kind has, and applies during resize as well as placement and
movement. The grid is anchored at the level origin.

**Undo/redo.** A stack of whole-`Level` snapshots, pushed on *completed*
operations (drag end, not per frame) and capped at 64 entries. Levels are
small enough that snapshotting is far cheaper to build and to trust than
per-command inverse logic.

---

## 6. Game integration

- `MainMenuScene` gains a third button, **Level Editor**, which pushes
  `EditorScene`.
- **Play** enumerates `assets/levels/`. When custom levels exist it presents a
  picker; when only the default does, it enters the level directly, so the extra
  click appears only once it is earned.
- `LevelScene` accepts an optional level path, loads it, and draws it through
  `LevelRenderer`. If the file is missing or malformed it falls back to today's
  hardcoded 600x400 rectangle — a broken level file must never prevent the game
  from booting, which matters with two developers sharing a repository.
- `LevelScene`'s hardcoded `level_size` is replaced by the loaded level's size.
  `UnitManager` is still constructed as a member, then given the real bounds via
  its existing `SetWorldBounds` once the level has loaded.
- A `assets/levels/default.level.json` ships with the build.

Nothing in this work implements collision, pathfinding or enemy routing. A level
loaded into the game is a static view of the arena.

---

## 7. Testing

There are none, at the user's explicit direction. This repository currently has
no test target, no test framework and no CI test step, and introducing the first
one was considered and declined for this piece of work.

The consequence worth recording: the serializer round-trip and the rotated-shape
hit-testing maths are the two places where defects will be silent and awkward to
diagnose through a GUI. If tests are ever added to this project, those are the
first two things to cover.

---

## Build changes

- `cmake/Dependencies.cmake` — add `nlohmann/json` under the find-or-fetch policy.
- `CMakeLists.txt` — add every new `.cpp` to `add_executable`, and link the JSON target.
- `tools/make_placeholder_atlas.py` — regenerate at 4x4 cells, 512x512, antialiased, with a triangle cell.
- `assets/levels/default.level.json` — new.
