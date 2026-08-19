# Level Editor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A graphical level editor reachable from the main menu that places, manipulates and recolours walls and edge markers, and loads/saves levels as JSON that the game can also read.

**Architecture:** A shared `horde::logic::Level` data model in the game code, serialised to JSON, rendered by a shared `horde::gfx::LevelRenderer` that both the editor and `LevelScene` call. The editor is a `Scene` whose UI is Dear ImGui panels and whose direct manipulation happens in world space through the existing `Camera2D`. All shapes draw as tinted, alpha-masked quads from the sprite atlas — no new render pipeline.

**Tech Stack:** C++20, SDL3 (3.4+) with SDL_GPU, glm, Dear ImGui (docking), nlohmann/json (new), CMake 3.28+ with presets.

**Spec:** `docs/superpowers/specs/2026-08-19-level-editor-design.md`

**Glossary:** `CONTEXT.md` (repo root). Use its vocabulary exactly: *wall*, *marker*, *spawn*, *exit*, *background*, *polyline*, *level*.

**ADRs:** `docs/adr/0001-shapes-as-textured-quads.md`, `docs/adr/0002-edge-relative-markers.md`

---

## Global Constraints

Every task's requirements implicitly include this section.

- **NO AUTOMATED TESTS.** This is an explicit, considered instruction from the project owner, recorded in spec section 7. This repository has no test target and none is to be added. Do **not** introduce Catch2, doctest, GoogleTest, a `tests/` directory, or a CTest target. Each task is verified by building and by the manual check written into that task. If you believe a test is needed, note it in the commit message; do not add one.
- **No exceptions.** Nothing in this codebase uses C++ exceptions. Catch `nlohmann::json` errors at the API boundary and convert them to an error string. Never let one escape a `logic::` function.
- **Warnings are part of the build.** Non-MSVC builds use `-Wall -Wextra -Wpedantic`; MSVC uses `/W4 /permissive-`. A task is not done if it added a warning.
- **World +y points down.** `Camera2D::viewProjection` (`src/gfx/Camera2D.cpp:47-48`) flips y. Consequences: an isoceles triangle's apex points toward **-y** to read as apex-up on screen, and a positive rotation appears **clockwise** on screen.
- **`gfx::Sprite::position` is the quad's TOP-LEFT corner and rotation is about that corner.** Every level element is defined by its centre, so drawing one needs `topLeft = center - R(theta) * (size * 0.5)`. Always use `gfx::drawCentered` (Task 1); never hand-roll this.
- **`gfx::Sprite` is `static_assert`ed to 64 bytes** and byte-matched to `shaders/src/sprite.vert.hlsl`. Do not add, remove or reorder its fields. No task in this plan touches any shader.
- **Level coordinates:** origin at top-left, bounds `(0,0)` to `(level.size)`.
- **File format:** rotations in **degrees** on disk, **radians** in memory. Colours are **0-255 integers** on disk, `Rgb` bytes in memory, `0.0-1.0` floats at the GPU.
- **Marker thickness:** fixed global constant, **12.0f** world units. **Undo depth:** **64** entries. **Snap defaults:** 10.0f world units, 15.0f degrees, both **off**.
- **Add every new `.cpp` to `add_executable` in `CMakeLists.txt`.** The build will not pick it up otherwise.
- **Formatting:** `.clang-format` (4-space indent, same-line braces, 120 columns). Run `clang-format -i` on touched files before committing.
- **Build command** (Linux, the dev platform): `cmake --build --preset linux-debug`. Run with `./build/linux-debug/bin/horde`.

---

## File Structure

**New — game-side (usable without the editor):**

| File | Responsibility |
|---|---|
| `src/logic/Level.hpp`, `src/logic/Level.cpp` | The `Level`, `Wall`, `Marker` data model, plus `makeDefaultLevel`, `markerRect`, `countMarkers`. |
| `src/logic/LevelGeometry.{hpp,cpp}` | `Aabb`, rotated-AABB of a wall, world/local point transforms. Shared by validation and hit-testing. |
| `src/logic/LevelValidation.{hpp,cpp}` | `validate()` -> list of `Problem`. |
| `src/logic/LevelIO.{hpp,cpp}` | JSON load and save. The only file that includes `nlohmann/json`. |
| `src/logic/LevelFiles.{hpp,cpp}` | Enumerating `assets/levels/`, resolving and sanitising save paths. In `logic/` because the main menu needs it too. |
| `src/gfx/LevelRenderer.{hpp,cpp}` | `renderLevel()`. Used by both the editor and `LevelScene`. |

**New — editor:**

| File | Responsibility |
|---|---|
| `src/scene/EditorScene.{hpp,cpp}` | Scene shell: camera, input routing, per-frame orchestration. |
| `src/editor/EditorState.hpp` | The level being edited, selection, active tool, snap settings, dirty flag. |
| `src/editor/UndoStack.hpp` | Whole-`Level` snapshots, capped at 64. |
| `src/editor/HitTest.{hpp,cpp}` | Point-in-wall and point-in-marker, in local space. |
| `src/editor/Tools.{hpp,cpp}` | Tool state machine and in-progress placement/drag state. |
| `src/editor/Handles.{hpp,cpp}` | Handle enumeration, hit-testing and drag response per wall kind. |
| `src/editor/Markers.{hpp,cpp}` | Edge projection, free-span computation and clamping for markers. |
| `src/editor/Snap.hpp` | Position and rotation snapping. Header-only. |
| `src/editor/EditorUi.{hpp,cpp}` | Every ImGui panel: toolbar, inspector, wall list, validation, file browser. |

**Modified:**

| File | Change |
|---|---|
| `tools/make_placeholder_atlas.py` | 4x4 cells at 128px, antialiased, new triangle cell. |
| `src/gfx/SpriteBatch.hpp` | Add the `drawCentered` free function. |
| `src/scene/LevelScene.{hpp,cpp}` | `atlasCell` grid 2x2 -> 4x4; load and render a level; drop hardcoded size. |
| `src/scene/TechTreeScene.cpp` | `atlasCell` grid 2x2 -> 4x4; use `drawCentered`. |
| `src/scene/MainMenuScene.{hpp,cpp}` | "Level Editor" button; level picker on Play. |
| `cmake/Dependencies.cmake` | Add nlohmann/json under the find-or-fetch policy. |
| `CMakeLists.txt` | Register new sources; link JSON. |

**New assets:** `assets/levels/default.level.json`

---

## Task 1: Atlas at 4x4, and the centre-origin draw helper

Everything later draws triangles and centred shapes. Both need to exist first, and this is the one task that changes existing scenes.

**Files:**
- Modify: `tools/make_placeholder_atlas.py` (whole file)
- Modify: `src/gfx/SpriteBatch.hpp` (add free function after `atlasCell`)
- Modify: `src/scene/LevelScene.cpp:44,52` (atlasCell calls)
- Modify: `src/scene/TechTreeScene.cpp:41,52` (atlasCell calls, and replace the hand-rolled centring at lines 35-46)
- Regenerate: `assets/textures/atlas.png`

**Interfaces:**
- Consumes: nothing.
- Produces: `gfx::drawCentered(SpriteBatch&, SDL_GPUTexture*, glm::vec2 center, glm::vec2 size, float rotation, glm::vec4 uv, glm::vec4 color, float z)`; atlas cell coordinates in a 4x4 grid, with the triangle at column 2, row 0.

- [ ] **Step 1: Rewrite the atlas generator for a 4x4, 512x512, antialiased atlas**

Replace `tools/make_placeholder_atlas.py` entirely:

```python
#!/usr/bin/env python3
"""Generates assets/textures/atlas.png, the placeholder sprite atlas.

The atlas is 512x512: a 4x4 grid of 128x128 cells, addressed by
gfx::atlasCell(col, row, 4, 4):

    (0,0) bordered square   tiles, rectangle walls
    (1,0) filled disc       unlocked tech node, circle walls, rotate handle
    (2,0) isoceles triangle triangle walls; apex at the TOP of the cell, which
                            is -y in world space, i.e. apex-up on screen
    (0,1) crossed disc      locked tech node
    (1,1) solid             lines, edges, flat fills, backgrounds, markers

Everything is drawn in white and shaped with alpha, so the per-sprite tint in
Sprite::color is what actually colours it. Alpha is supersampled 4x4 per texel
so edges stay smooth when a cell is scaled up to a large world-space wall.

Committed output, so this only needs re-running if the placeholder art changes.
"""

import pathlib
import struct
import zlib

COLS = 4
ROWS = 4
CELL = 128
WIDTH = COLS * CELL
HEIGHT = ROWS * CELL
SUBSAMPLES = 4


def coverage(fn, lx, ly):
    """Fraction of a texel covered, by supersampling `fn` on a SUBSAMPLES grid."""
    hits = 0
    step = 1.0 / SUBSAMPLES
    for sy in range(SUBSAMPLES):
        for sx in range(SUBSAMPLES):
            if fn(lx + (sx + 0.5) * step, ly + (sy + 0.5) * step):
                hits += 1
    return hits / (SUBSAMPLES * SUBSAMPLES)


def in_disc(x, y):
    dx = x - CELL / 2.0
    dy = y - CELL / 2.0
    r = CELL * 0.5 - 0.5
    return dx * dx + dy * dy <= r * r


def in_triangle(x, y):
    """Isoceles triangle filling the cell, apex at top-centre, base at bottom."""
    # Half-width grows linearly from 0 at the apex to CELL/2 at the base.
    half = (y / CELL) * (CELL / 2.0)
    return abs(x - CELL / 2.0) <= half


def cell_pixel(col, row, lx, ly):
    """Returns (r, g, b, a) for one texel of cell (col, row)."""
    if (col, row) == (0, 0):
        border = CELL // 16
        edge = lx < border or ly < border or lx >= CELL - border or ly >= CELL - border
        return (255, 255, 255, 255 if edge else 190)

    if (col, row) == (1, 0):
        return (255, 255, 255, round(255 * coverage(in_disc, lx, ly)))

    if (col, row) == (2, 0):
        return (255, 255, 255, round(255 * coverage(in_triangle, lx, ly)))

    if (col, row) == (0, 1):
        alpha = coverage(in_disc, lx, ly)
        if alpha == 0.0:
            return (255, 255, 255, 0)
        # Punch a diagonal slash out of the disc to read as "locked".
        if abs(lx - ly) <= CELL // 16:
            return (255, 255, 255, round(60 * alpha))
        return (255, 255, 255, round(255 * alpha))

    return (255, 255, 255, 255)


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    out = root / "assets" / "textures" / "atlas.png"
    out.parent.mkdir(parents=True, exist_ok=True)

    raw = bytearray()
    for y in range(HEIGHT):
        raw.append(0)  # PNG filter type 0 (None) for this scanline
        row = y // CELL
        ly = y % CELL
        for x in range(WIDTH):
            raw.extend(cell_pixel(x // CELL, row, x % CELL, ly))

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")

    out.write_bytes(png)
    print(f"wrote {out} ({len(png)} bytes)")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Regenerate the atlas and confirm its dimensions**

Run:
```bash
python3 tools/make_placeholder_atlas.py
python3 -c "import struct;d=open('assets/textures/atlas.png','rb').read();print(struct.unpack('>II', d[16:24]))"
```
Expected: `(512, 512)`.

- [ ] **Step 3: Add the `drawCentered` helper**

In `src/gfx/SpriteBatch.hpp`, add `#include <cmath>` to the includes. Add this function at the **bottom of the file**, just before the closing `} // namespace horde::gfx` — it must come after the `SpriteBatch` class declaration because it calls `batch.draw`:

```cpp
// Queues a sprite centred on `center` and rotated about that centre.
//
// Sprite::position is the quad's TOP-LEFT corner and Sprite::rotation turns the
// quad about that corner, so a centred shape must have its corner placed at
// center - R(rotation) * (size * 0.5). Every caller that thinks in centres —
// which is everything drawing a level — must go through here rather than
// repeating this trigonometry.
inline void drawCentered(SpriteBatch& batch, SDL_GPUTexture* texture, glm::vec2 center, glm::vec2 size, float rotation,
                         glm::vec4 uv, glm::vec4 color, float z = 0.0f) {
    const glm::vec2 half = size * 0.5f;
    const float c = std::cos(rotation);
    const float s = std::sin(rotation);
    const glm::vec2 offset{half.x * c - half.y * s, half.x * s + half.y * c};

    Sprite sprite;
    sprite.position = {center.x - offset.x, center.y - offset.y, z};
    sprite.rotation = rotation;
    sprite.size = size;
    sprite.uv = uv;
    sprite.color = color;

    batch.draw(sprite, texture);
}
```

Leave a one-line comment next to `atlasCell` pointing readers down to it.

- [ ] **Step 4: Update every existing `atlasCell` call from a 2x2 to a 4x4 grid**

Run this to find them all:
```bash
grep -rn "atlasCell" src/
```
Expected: 4 call sites — `LevelScene.cpp` (2) and `TechTreeScene.cpp` (2). Change every `atlasCell(c, r, 2, 2)` to `atlasCell(c, r, 4, 4)`, leaving `c` and `r` untouched. Verify none remain:
```bash
grep -rn "atlasCell(.*, 2, 2)" src/ || echo "all updated"
```

- [ ] **Step 5: Replace the hand-rolled centring in TechTreeScene**

In `src/scene/TechTreeScene.cpp`, the node loop currently offsets by `kNodeSize * 0.5f` by hand. Replace the whole `for (const glm::vec2& position : {m_nodeA, m_nodeB})` body with:

```cpp
    for (const glm::vec2& position : {m_nodeA, m_nodeB}) {
        gfx::drawCentered(batch, atlas, position, {kNodeSize, kNodeSize}, 0.0f, gfx::atlasCell(1, 0, 4, 4),
                          {0.7f, 0.9f, 0.7f, 1.0f}, 0.1f);
    }
```

Leave the edge quad as it is — it is centred on a line rather than a point, and its existing normal-offset maths is correct.

- [ ] **Step 6: Build and verify visually**

Run:
```bash
clang-format -i src/gfx/SpriteBatch.hpp src/scene/TechTreeScene.cpp src/scene/LevelScene.cpp
cmake --build --preset linux-debug 2>&1 | tail -20
./build/linux-debug/bin/horde
```
Expected: builds with no new warnings. Click **Play** — the blue rectangle and green units look exactly as they did before. Click back, then **Upgrades** — the two round nodes and the connecting line look exactly as they did before, and the discs have visibly *smoother* edges than previously. If any sprite is now sampling the wrong artwork, a call site was missed in Step 4.

- [ ] **Step 7: Commit**

```bash
git add tools/make_placeholder_atlas.py assets/textures/atlas.png src/gfx/SpriteBatch.hpp src/scene/TechTreeScene.cpp src/scene/LevelScene.cpp
git commit -m "Grow sprite atlas to 4x4 and add a centre-origin draw helper

The level editor needs a triangle cell and draws everything from centres
rather than corners. Regenerates the atlas at 512x512 with supersampled
alpha so shapes stay smooth when scaled up to wall size, and adds
gfx::drawCentered so the top-left-corner rotation maths lives in one place.

Existing atlasCell call sites move from a 2x2 to a 4x4 grid."
```

---

## Task 2: The level data model and its geometry

**Files:**
- Create: `src/logic/Level.hpp`, `src/logic/Level.cpp`
- Create: `src/logic/LevelGeometry.hpp`, `src/logic/LevelGeometry.cpp`
- Create: `src/logic/LevelValidation.hpp`, `src/logic/LevelValidation.cpp`
- Modify: `CMakeLists.txt` (add the two new `.cpp` files)

**Interfaces:**
- Consumes: nothing.
- Produces: the entire `horde::logic` level vocabulary — `Rgb`, `RectangleShape`, `TriangleShape`, `CircleShape`, `PolylineShape`, `Wall`, `Edge`, `MarkerKind`, `Marker`, `Level`, `makeDefaultLevel()`, `markerRect()`, `edgeLength()`, `Aabb`, `wallAabb()`, `worldToLocal()`, `Problem`, `validate()`.

- [ ] **Step 1: Create `src/logic/Level.hpp`**

```cpp
#pragma once

#include <glm/vec2.hpp>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace horde::logic {

// Fixed thickness of every marker, in world units. Markers stretch along their
// edge only, so this is the one dimension no one authors.
inline constexpr float kMarkerThickness = 12.0f;

// A colour with no alpha. Walls are opaque: see spec section 1.
struct Rgb {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
};

struct RectangleShape {
    glm::vec2 halfExtents{25.0f, 25.0f};
};

// Isoceles. The apex points toward -y in the wall's local space, which is
// upward on screen because world +y points down.
struct TriangleShape {
    glm::vec2 halfExtents{25.0f, 25.0f};
};

struct CircleShape {
    float radius = 25.0f;
};

// An ordered run of points joined by straight segments of uniform thickness.
// Points are in the wall's LOCAL space, relative to its centre, so that moving
// and rotating a polyline works exactly as it does for every other wall kind.
// The run need not be closed.
struct PolylineShape {
    std::vector<glm::vec2> points;
    float thickness = 6.0f;
};

using WallShape = std::variant<RectangleShape, TriangleShape, CircleShape, PolylineShape>;

// A shape units will collide with. Every placed shape is a wall.
//
// `rotation` is present but ignored for CircleShape. Keeping one transform for
// every kind is worth one unused float: see spec section 1.
struct Wall {
    glm::vec2 center{0.0f, 0.0f};
    float rotation = 0.0f; // radians; positive is clockwise on screen
    Rgb color{180, 180, 190};
    WallShape shape;
};

enum class Edge { North, South, East, West };
enum class MarkerKind { Spawn, Exit };

// A spawn or an exit: a thin band lying flat against one level edge.
//
// Stored edge-relative rather than positioned so that "cannot move toward the
// centre" is unrepresentable rather than merely validated. See
// docs/adr/0002-edge-relative-markers.md.
//
// `offset` is the distance along the edge from its start corner; `length` is
// how far it extends from there. Both are in world units.
struct Marker {
    MarkerKind kind = MarkerKind::Spawn;
    Edge edge = Edge::West;
    float offset = 0.0f;
    float length = 100.0f;
};

// A rectangular arena and everything placed in it.
//
// Bounds run from (0,0) to `size`. The background exactly fills those bounds
// and is the only element units do not collide with.
struct Level {
    glm::vec2 size{600.0f, 400.0f};
    Rgb backgroundColor{38, 42, 50};
    std::vector<Wall> walls;      // a later index draws on top of an earlier one
    std::vector<Marker> markers;  // always at least one spawn and one exit
};

// Length of one edge of a level, in world units.
inline float edgeLength(const Level& level, Edge edge) {
    return (edge == Edge::North || edge == Edge::South) ? level.size.x : level.size.y;
}

// A blank level: default size, one spawn centred on the west edge, one exit
// centred on the east.
Level makeDefaultLevel();

// The axis-aligned world-space rectangle a marker occupies, as centre and size.
// Markers are drawn inward from their edge.
void markerRect(const Level& level, const Marker& marker, glm::vec2& outCenter, glm::vec2& outSize);

// How many markers of this kind the level has. The last spawn and the last exit
// cannot be deleted.
std::size_t countMarkers(const Level& level, MarkerKind kind);

} // namespace horde::logic
```

- [ ] **Step 2: Create `src/logic/LevelGeometry.hpp`**

```cpp
#pragma once

#include <glm/vec2.hpp>

#include "logic/Level.hpp"

namespace horde::logic {

// An axis-aligned bounding box in world space.
struct Aabb {
    glm::vec2 min{0.0f, 0.0f};
    glm::vec2 max{0.0f, 0.0f};

    bool contains(glm::vec2 point) const {
        return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
    }

    // True if this box lies entirely within `other`.
    bool within(const Aabb& other) const {
        return min.x >= other.min.x && min.y >= other.min.y && max.x <= other.max.x && max.y <= other.max.y;
    }
};

// The bounds of a level, as a box.
Aabb levelBounds(const Level& level);

// A wall's untransformed extent, centred on the origin. For a polyline this is
// the box around its points, grown by half the line thickness.
Aabb localWallBounds(const Wall& wall);

// A wall's world-space bounds after rotation: the box around the four rotated
// corners of its local bounds. Conservative for circles and triangles, which is
// correct for the uses here (out-of-bounds validation and broad-phase picking).
Aabb wallAabb(const Wall& wall);

// Transforms a world-space point into a wall's local space, undoing the wall's
// translation and rotation. This is what makes hit-testing and handle dragging
// correct at arbitrary angles: work in local space, where the shape is axis
// aligned.
glm::vec2 worldToLocal(const Wall& wall, glm::vec2 world);

// The inverse of worldToLocal.
glm::vec2 localToWorld(const Wall& wall, glm::vec2 local);

} // namespace horde::logic
```

- [ ] **Step 3: Create `src/logic/LevelGeometry.cpp`**

```cpp
#include "logic/LevelGeometry.hpp"

#include <algorithm>
#include <cmath>

namespace horde::logic {
namespace {

Aabb boxAround(glm::vec2 halfExtents) {
    return Aabb{-halfExtents, halfExtents};
}

} // namespace

Aabb levelBounds(const Level& level) {
    return Aabb{{0.0f, 0.0f}, level.size};
}

Aabb localWallBounds(const Wall& wall) {
    if (const auto* rect = std::get_if<RectangleShape>(&wall.shape)) {
        return boxAround(rect->halfExtents);
    }
    if (const auto* tri = std::get_if<TriangleShape>(&wall.shape)) {
        return boxAround(tri->halfExtents);
    }
    if (const auto* circle = std::get_if<CircleShape>(&wall.shape)) {
        return boxAround({circle->radius, circle->radius});
    }

    const auto& line = std::get<PolylineShape>(wall.shape);
    if (line.points.empty()) {
        return Aabb{};
    }

    Aabb bounds{line.points.front(), line.points.front()};
    for (const glm::vec2& point : line.points) {
        bounds.min = glm::vec2{std::min(bounds.min.x, point.x), std::min(bounds.min.y, point.y)};
        bounds.max = glm::vec2{std::max(bounds.max.x, point.x), std::max(bounds.max.y, point.y)};
    }

    const float half = line.thickness * 0.5f;
    bounds.min -= glm::vec2{half, half};
    bounds.max += glm::vec2{half, half};
    return bounds;
}

Aabb wallAabb(const Wall& wall) {
    const Aabb local = localWallBounds(wall);
    const glm::vec2 corners[4] = {
        {local.min.x, local.min.y},
        {local.max.x, local.min.y},
        {local.min.x, local.max.y},
        {local.max.x, local.max.y},
    };

    Aabb world{localToWorld(wall, corners[0]), localToWorld(wall, corners[0])};
    for (int i = 1; i < 4; ++i) {
        const glm::vec2 p = localToWorld(wall, corners[i]);
        world.min = glm::vec2{std::min(world.min.x, p.x), std::min(world.min.y, p.y)};
        world.max = glm::vec2{std::max(world.max.x, p.x), std::max(world.max.y, p.y)};
    }
    return world;
}

glm::vec2 worldToLocal(const Wall& wall, glm::vec2 world) {
    const glm::vec2 d = world - wall.center;
    const float c = std::cos(-wall.rotation);
    const float s = std::sin(-wall.rotation);
    return {d.x * c - d.y * s, d.x * s + d.y * c};
}

glm::vec2 localToWorld(const Wall& wall, glm::vec2 local) {
    const float c = std::cos(wall.rotation);
    const float s = std::sin(wall.rotation);
    return wall.center + glm::vec2{local.x * c - local.y * s, local.x * s + local.y * c};
}

} // namespace horde::logic
```

- [ ] **Step 4: Create `src/logic/LevelValidation.hpp`**

```cpp
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "logic/Level.hpp"

namespace horde::logic {

// One reason a level cannot be saved.
struct Problem {
    std::size_t wallIndex = 0; // index into Level::walls
    std::string message;       // shown to the user verbatim
};

// Every reason this level cannot be saved, in wall order. Empty means valid.
//
// Only walls are checked. Marker invariants — staying on an edge, not
// overlapping, always having at least one spawn and one exit — are maintained
// by construction in the editor rather than validated here. See spec section 3.
std::vector<Problem> validate(const Level& level);

} // namespace horde::logic
```

- [ ] **Step 5: Create `src/logic/LevelValidation.cpp`**

```cpp
#include "logic/LevelValidation.hpp"

#include "logic/LevelGeometry.hpp"

namespace horde::logic {

std::vector<Problem> validate(const Level& level) {
    std::vector<Problem> problems;
    const Aabb bounds = levelBounds(level);

    for (std::size_t i = 0; i < level.walls.size(); ++i) {
        const Wall& wall = level.walls[i];

        bool degenerate = false;
        if (const auto* rect = std::get_if<RectangleShape>(&wall.shape)) {
            degenerate = rect->halfExtents.x <= 0.0f || rect->halfExtents.y <= 0.0f;
        } else if (const auto* tri = std::get_if<TriangleShape>(&wall.shape)) {
            degenerate = tri->halfExtents.x <= 0.0f || tri->halfExtents.y <= 0.0f;
        } else if (const auto* circle = std::get_if<CircleShape>(&wall.shape)) {
            degenerate = circle->radius <= 0.0f;
        } else {
            const auto& line = std::get<PolylineShape>(wall.shape);
            degenerate = line.points.size() < 2 || line.thickness <= 0.0f;
        }

        if (degenerate) {
            problems.push_back({i, "Wall has zero or negative size"});
            continue; // an empty shape has no meaningful bounds to test
        }

        if (!wallAabb(wall).within(bounds)) {
            problems.push_back({i, "Wall lies outside the level bounds"});
        }
    }

    return problems;
}

} // namespace horde::logic
```

- [ ] **Step 6: Create `src/logic/Level.cpp` for the three out-of-line functions**

```cpp
#include "logic/Level.hpp"

namespace horde::logic {

Level makeDefaultLevel() {
    Level level;

    Marker spawn;
    spawn.kind = MarkerKind::Spawn;
    spawn.edge = Edge::West;
    spawn.length = 100.0f;
    spawn.offset = (level.size.y - spawn.length) * 0.5f;

    Marker exit;
    exit.kind = MarkerKind::Exit;
    exit.edge = Edge::East;
    exit.length = 100.0f;
    exit.offset = (level.size.y - exit.length) * 0.5f;

    level.markers.push_back(spawn);
    level.markers.push_back(exit);
    return level;
}

void markerRect(const Level& level, const Marker& marker, glm::vec2& outCenter, glm::vec2& outSize) {
    const float mid = marker.offset + marker.length * 0.5f;
    const float halfThickness = kMarkerThickness * 0.5f;

    switch (marker.edge) {
        case Edge::North:
            outCenter = {mid, halfThickness};
            outSize = {marker.length, kMarkerThickness};
            break;
        case Edge::South:
            outCenter = {mid, level.size.y - halfThickness};
            outSize = {marker.length, kMarkerThickness};
            break;
        case Edge::West:
            outCenter = {halfThickness, mid};
            outSize = {kMarkerThickness, marker.length};
            break;
        case Edge::East:
            outCenter = {level.size.x - halfThickness, mid};
            outSize = {kMarkerThickness, marker.length};
            break;
    }
}

std::size_t countMarkers(const Level& level, MarkerKind kind) {
    std::size_t count = 0;
    for (const Marker& marker : level.markers) {
        if (marker.kind == kind) {
            ++count;
        }
    }
    return count;
}

} // namespace horde::logic
```

- [ ] **Step 7: Register the new sources in `CMakeLists.txt`**

In the `add_executable(horde ...)` list, after `src/logic/UnitData.hpp`, add:

```cmake
    src/logic/Level.cpp
    src/logic/LevelGeometry.cpp
    src/logic/LevelValidation.cpp
```

- [ ] **Step 8: Build**

Run:
```bash
clang-format -i src/logic/Level.hpp src/logic/Level.cpp src/logic/LevelGeometry.hpp src/logic/LevelGeometry.cpp src/logic/LevelValidation.hpp src/logic/LevelValidation.cpp
cmake --build --preset linux-debug 2>&1 | tail -20
```
Expected: builds cleanly, no warnings. Nothing calls this code yet, so there is nothing to run — the next task gives it a consumer.

- [ ] **Step 9: Commit**

```bash
git add src/logic/Level.hpp src/logic/Level.cpp src/logic/LevelGeometry.hpp src/logic/LevelGeometry.cpp src/logic/LevelValidation.hpp src/logic/LevelValidation.cpp CMakeLists.txt
git commit -m "Add the level data model, geometry and validation

A Level is bounds, a background colour, a list of walls and a list of
markers. A wall is a shared transform plus a variant payload per kind, so
that adding per-vertex polyline handles later touches one case rather
than every branch of a switch.

Markers are stored edge-relative so that moving one toward the level
centre is unrepresentable rather than validated against."
```

---

## Task 3: JSON load and save

**Files:**
- Create: `src/logic/LevelIO.hpp`, `src/logic/LevelIO.cpp`
- Create: `assets/levels/default.level.json`
- Modify: `cmake/Dependencies.cmake` (add nlohmann/json)
- Modify: `CMakeLists.txt` (link JSON, register `LevelIO.cpp`)

**Interfaces:**
- Consumes: `logic::Level` and everything in `logic/Level.hpp` (Task 2).
- Produces: `logic::loadLevel(const std::filesystem::path&, std::string* error) -> std::optional<Level>` and `logic::saveLevel(const Level&, const std::filesystem::path&, std::string* error) -> bool`.

- [ ] **Step 1: Add nlohmann/json to `cmake/Dependencies.cmake`**

Add `set(HORDE_JSON_TAG "v3.11.3" CACHE STRING "nlohmann/json tag to fetch when no system copy is found")` alongside the other `HORDE_*_TAG` variables at the top, then append this section before the Dear ImGui one:

```cmake
# --- nlohmann/json -----------------------------------------------------------
#
# Header-only, and the only parsing dependency in the project. Used solely by
# src/logic/LevelIO.cpp to read and write level files.

find_package(nlohmann_json 3.11 CONFIG QUIET)

if(nlohmann_json_FOUND)
    message(STATUS "horde: using system nlohmann/json ${nlohmann_json_VERSION}")
else()
    message(STATUS "horde: no system nlohmann/json, fetching ${HORDE_JSON_TAG}")
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    set(JSON_Install OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG ${HORDE_JSON_TAG}
        GIT_SHALLOW ON
        EXCLUDE_FROM_ALL
        SYSTEM)
    FetchContent_MakeAvailable(nlohmann_json)
endif()
```

In `CMakeLists.txt`, add `nlohmann_json::nlohmann_json` to `target_link_libraries(horde PRIVATE ...)` and `src/logic/LevelIO.cpp` to `add_executable`.

- [ ] **Step 2: Reconfigure so CMake picks up the new dependency**

Run:
```bash
cmake --preset linux-debug 2>&1 | grep -i "json"
```
Expected: one `horde:` line saying either "using system nlohmann/json <version>" or "no system nlohmann/json, fetching v3.11.3". If it fetches, this takes a minute — that is normal and one-time.

- [ ] **Step 3: Create `src/logic/LevelIO.hpp`**

```cpp
#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "logic/Level.hpp"

namespace horde::logic {

// The schema version this build writes, and the only one it reads.
inline constexpr int kLevelFormatVersion = 1;

// Reads a level from disk.
//
// Returns nullopt on any failure — missing file, malformed JSON, unrecognised
// version, missing required key — writing a human-readable reason into `error`
// when it is non-null. No exception ever escapes: callers are expected to fall
// back rather than to catch.
std::optional<Level> loadLevel(const std::filesystem::path& path, std::string* error);

// Writes a level to disk, creating parent directories as needed. Returns false
// and fills `error` on failure.
//
// This does NOT validate. Callers that must not write a broken level are
// responsible for calling logic::validate first.
bool saveLevel(const Level& level, const std::filesystem::path& path, std::string* error);

} // namespace horde::logic
```

- [ ] **Step 4: Create `src/logic/LevelIO.cpp`**

The whole file. Note `json::parse(..., nullptr, false)` — the third argument disables exceptions and makes a parse failure return a discarded value instead.

```cpp
#include "logic/LevelIO.hpp"

#include <nlohmann/json.hpp>

#include <glm/trigonometric.hpp>

#include <fstream>

namespace horde::logic {
namespace {

using nlohmann::json;

// Every reader below reports a missing or wrongly-typed key as a failure rather
// than defaulting it. A level file missing a wall's centre is a corrupt file,
// not a file with a wall at the origin.
bool readFloat(const json& node, const char* key, float& out, std::string* error) {
    if (!node.contains(key) || !node[key].is_number()) {
        if (error) {
            *error = std::string("missing or non-numeric key: ") + key;
        }
        return false;
    }
    out = node[key].get<float>();
    return true;
}

bool readVec2(const json& node, const char* key, glm::vec2& out, std::string* error) {
    if (!node.contains(key) || !node[key].is_array() || node[key].size() != 2 || !node[key][0].is_number() ||
        !node[key][1].is_number()) {
        if (error) {
            *error = std::string("key is not a two-number array: ") + key;
        }
        return false;
    }
    out = glm::vec2{node[key][0].get<float>(), node[key][1].get<float>()};
    return true;
}

bool readRgb(const json& node, const char* key, Rgb& out, std::string* error) {
    if (!node.contains(key) || !node[key].is_array() || node[key].size() != 3) {
        if (error) {
            *error = std::string("key is not a three-number array: ") + key;
        }
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (!node[key][i].is_number_integer()) {
            if (error) {
                *error = std::string("colour component is not an integer: ") + key;
            }
            return false;
        }
    }
    out.r = static_cast<std::uint8_t>(std::clamp(node[key][0].get<int>(), 0, 255));
    out.g = static_cast<std::uint8_t>(std::clamp(node[key][1].get<int>(), 0, 255));
    out.b = static_cast<std::uint8_t>(std::clamp(node[key][2].get<int>(), 0, 255));
    return true;
}

json writeVec2(glm::vec2 v) {
    return json::array({v.x, v.y});
}

json writeRgb(Rgb c) {
    return json::array({static_cast<int>(c.r), static_cast<int>(c.g), static_cast<int>(c.b)});
}

const char* edgeName(Edge edge) {
    switch (edge) {
        case Edge::North: return "north";
        case Edge::South: return "south";
        case Edge::East:  return "east";
        case Edge::West:  return "west";
    }
    return "west";
}

bool parseEdge(const std::string& name, Edge& out) {
    if (name == "north") { out = Edge::North; return true; }
    if (name == "south") { out = Edge::South; return true; }
    if (name == "east")  { out = Edge::East;  return true; }
    if (name == "west")  { out = Edge::West;  return true; }
    return false;
}

bool readWall(const json& node, Wall& wall, std::string* error) {
    if (!node.contains("kind") || !node["kind"].is_string()) {
        if (error) { *error = "wall has no 'kind'"; }
        return false;
    }

    if (!readVec2(node, "center", wall.center, error) || !readRgb(node, "color", wall.color, error)) {
        return false;
    }

    // Circles omit rotation, since it is meaningless for them. Any wall without
    // the key reads back as zero. This is the ONLY optional key.
    float degrees = 0.0f;
    if (node.contains("rotation")) {
        if (!readFloat(node, "rotation", degrees, error)) {
            return false;
        }
    }
    wall.rotation = glm::radians(degrees);

    const std::string kind = node["kind"].get<std::string>();

    if (kind == "rectangle" || kind == "triangle") {
        glm::vec2 halfExtents{};
        if (!readVec2(node, "halfExtents", halfExtents, error)) {
            return false;
        }
        if (kind == "rectangle") {
            wall.shape = RectangleShape{halfExtents};
        } else {
            wall.shape = TriangleShape{halfExtents};
        }
        return true;
    }

    if (kind == "circle") {
        float radius = 0.0f;
        if (!readFloat(node, "radius", radius, error)) {
            return false;
        }
        wall.shape = CircleShape{radius};
        return true;
    }

    if (kind == "polyline") {
        PolylineShape line;
        if (!readFloat(node, "thickness", line.thickness, error)) {
            return false;
        }
        if (!node.contains("points") || !node["points"].is_array()) {
            if (error) { *error = "polyline has no 'points' array"; }
            return false;
        }
        for (const json& point : node["points"]) {
            if (!point.is_array() || point.size() != 2 || !point[0].is_number() || !point[1].is_number()) {
                if (error) { *error = "polyline point is not a two-number array"; }
                return false;
            }
            line.points.push_back(glm::vec2{point[0].get<float>(), point[1].get<float>()});
        }
        wall.shape = std::move(line);
        return true;
    }

    if (error) { *error = "unknown wall kind: " + kind; }
    return false;
}

json writeWall(const Wall& wall) {
    json node;
    node["center"] = writeVec2(wall.center);
    node["color"] = writeRgb(wall.color);

    if (const auto* rect = std::get_if<RectangleShape>(&wall.shape)) {
        node["kind"] = "rectangle";
        node["rotation"] = glm::degrees(wall.rotation);
        node["halfExtents"] = writeVec2(rect->halfExtents);
    } else if (const auto* tri = std::get_if<TriangleShape>(&wall.shape)) {
        node["kind"] = "triangle";
        node["rotation"] = glm::degrees(wall.rotation);
        node["halfExtents"] = writeVec2(tri->halfExtents);
    } else if (const auto* circle = std::get_if<CircleShape>(&wall.shape)) {
        node["kind"] = "circle"; // no rotation: meaningless for a circle
        node["radius"] = circle->radius;
    } else {
        const auto& line = std::get<PolylineShape>(wall.shape);
        node["kind"] = "polyline";
        node["rotation"] = glm::degrees(wall.rotation);
        node["thickness"] = line.thickness;
        json points = json::array();
        for (const glm::vec2& point : line.points) {
            points.push_back(writeVec2(point));
        }
        node["points"] = std::move(points);
    }

    return node;
}

} // namespace

std::optional<Level> loadLevel(const std::filesystem::path& path, std::string* error) {
    std::ifstream stream(path);
    if (!stream) {
        if (error) { *error = "cannot open " + path.string(); }
        return std::nullopt;
    }

    const json root = json::parse(stream, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        if (error) { *error = "malformed JSON in " + path.string(); }
        return std::nullopt;
    }

    if (!root.contains("version") || !root["version"].is_number_integer() ||
        root["version"].get<int>() != kLevelFormatVersion) {
        if (error) { *error = "unsupported or missing level format version"; }
        return std::nullopt;
    }

    Level level;
    if (!readVec2(root, "size", level.size, error) ||
        !readRgb(root, "backgroundColor", level.backgroundColor, error)) {
        return std::nullopt;
    }

    if (root.contains("walls")) {
        if (!root["walls"].is_array()) {
            if (error) { *error = "'walls' is not an array"; }
            return std::nullopt;
        }
        for (const json& node : root["walls"]) {
            Wall wall;
            if (!readWall(node, wall, error)) {
                return std::nullopt;
            }
            level.walls.push_back(std::move(wall));
        }
    }

    if (root.contains("markers")) {
        if (!root["markers"].is_array()) {
            if (error) { *error = "'markers' is not an array"; }
            return std::nullopt;
        }
        for (const json& node : root["markers"]) {
            Marker marker;
            if (!node.contains("kind") || !node["kind"].is_string() || !node.contains("edge") ||
                !node["edge"].is_string()) {
                if (error) { *error = "marker is missing 'kind' or 'edge'"; }
                return std::nullopt;
            }
            const std::string kind = node["kind"].get<std::string>();
            if (kind == "spawn") {
                marker.kind = MarkerKind::Spawn;
            } else if (kind == "exit") {
                marker.kind = MarkerKind::Exit;
            } else {
                if (error) { *error = "unknown marker kind: " + kind; }
                return std::nullopt;
            }
            if (!parseEdge(node["edge"].get<std::string>(), marker.edge)) {
                if (error) { *error = "unknown marker edge"; }
                return std::nullopt;
            }
            if (!readFloat(node, "offset", marker.offset, error) ||
                !readFloat(node, "length", marker.length, error)) {
                return std::nullopt;
            }
            level.markers.push_back(marker);
        }
    }

    // A level on disk with no markers predates nothing and means a hand-edited
    // file lost them. Restore the minimum rather than refusing to load.
    if (countMarkers(level, MarkerKind::Spawn) == 0) {
        level.markers.push_back(Marker{MarkerKind::Spawn, Edge::West, (level.size.y - 100.0f) * 0.5f, 100.0f});
    }
    if (countMarkers(level, MarkerKind::Exit) == 0) {
        level.markers.push_back(Marker{MarkerKind::Exit, Edge::East, (level.size.y - 100.0f) * 0.5f, 100.0f});
    }

    return level;
}

bool saveLevel(const Level& level, const std::filesystem::path& path, std::string* error) {
    json root;
    root["version"] = kLevelFormatVersion;
    root["size"] = writeVec2(level.size);
    root["backgroundColor"] = writeRgb(level.backgroundColor);

    json walls = json::array();
    for (const Wall& wall : level.walls) {
        walls.push_back(writeWall(wall));
    }
    root["walls"] = std::move(walls);

    json markers = json::array();
    for (const Marker& marker : level.markers) {
        json node;
        node["kind"] = marker.kind == MarkerKind::Spawn ? "spawn" : "exit";
        node["edge"] = edgeName(marker.edge);
        node["offset"] = marker.offset;
        node["length"] = marker.length;
        markers.push_back(std::move(node));
    }
    root["markers"] = std::move(markers);

    std::error_code code;
    std::filesystem::create_directories(path.parent_path(), code);

    std::ofstream stream(path, std::ios::trunc);
    if (!stream) {
        if (error) { *error = "cannot write " + path.string(); }
        return false;
    }

    stream << root.dump(2) << '\n';
    if (!stream) {
        if (error) { *error = "write failed for " + path.string(); }
        return false;
    }

    return true;
}

} // namespace horde::logic
```

Add `#include <algorithm>` at the top for `std::clamp`.

- [ ] **Step 5: Create `assets/levels/default.level.json`**

A hand-written file exercising every wall kind, so that Task 4's render is a real check rather than an empty box:

```json
{
  "version": 1,
  "size": [600, 400],
  "backgroundColor": [38, 42, 50],
  "walls": [
    { "kind": "rectangle", "center": [180, 120], "rotation": 0, "color": [200, 90, 80], "halfExtents": [60, 20] },
    { "kind": "rectangle", "center": [420, 280], "rotation": 30, "color": [200, 150, 80], "halfExtents": [70, 18] },
    { "kind": "triangle", "center": [300, 200], "rotation": 0, "color": [110, 180, 110], "halfExtents": [40, 55] },
    { "kind": "circle", "center": [460, 110], "color": [90, 130, 220], "radius": 45 },
    { "kind": "polyline", "center": [160, 300], "rotation": 0, "color": [220, 210, 120], "thickness": 8,
      "points": [[-60, -30], [0, 20], [60, -30], [90, 10]] }
  ],
  "markers": [
    { "kind": "spawn", "edge": "west", "offset": 150, "length": 100 },
    { "kind": "exit", "edge": "east", "offset": 150, "length": 100 }
  ]
}
```

The `assets/` directory is copied next to the executable by the existing `horde_runtime_files` post-build step, so this ships with the build automatically.

- [ ] **Step 6: Build**

Run:
```bash
clang-format -i src/logic/LevelIO.hpp src/logic/LevelIO.cpp
cmake --build --preset linux-debug 2>&1 | tail -20
python3 -c "import json;json.load(open('assets/levels/default.level.json'));print('default level is valid JSON')"
```
Expected: builds cleanly with no warnings, and the JSON parses. Task 4 gives this a visible consumer.

- [ ] **Step 7: Commit**

```bash
git add src/logic/LevelIO.hpp src/logic/LevelIO.cpp assets/levels/default.level.json cmake/Dependencies.cmake CMakeLists.txt
git commit -m "Add JSON level serialization

Levels are hand-editable JSON with rotations in degrees and colours as
0-255 integers. Every key except a wall's rotation is required; a missing
key is a load error rather than a silent default, since a file missing a
wall's centre is corrupt rather than sparse.

nlohmann/json joins the dependency list under the existing find-system-
or-fetch policy. Parsing runs with exceptions disabled so no exception
can escape into a codebase that does not use them."
```

---

## Task 4: Render a level, and make `LevelScene` load one

This is the task that makes Tasks 2 and 3 visible. After it, `Play` shows the default level instead of a bare rectangle.

**Files:**
- Create: `src/gfx/LevelRenderer.hpp`, `src/gfx/LevelRenderer.cpp`
- Modify: `src/scene/LevelScene.hpp` (level member, path, drop hardcoded size)
- Modify: `src/scene/LevelScene.cpp` (load in `onEnter`, render via `LevelRenderer`)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `logic::Level` (Task 2), `logic::loadLevel` (Task 3), `gfx::drawCentered` and the 4x4 atlas (Task 1).
- Produces: `gfx::renderLevel(const logic::Level&, SpriteBatch&, SDL_GPUTexture*)`, and the atlas-cell constants `gfx::kCellSquare`, `kCellDisc`, `kCellTriangle`, `kCellSolid`.

**Critical ordering note:** the `SpriteBatch` pipeline sets a blend state but **no depth-stencil state** (`src/gfx/SpriteBatch.cpp:40-51`), so there is **no depth testing**. `Sprite::position.z` does not affect what draws on top — **submission order does**. That is exactly what the spec wants ("a later index draws on top"), so simply draw in order: background, then walls front-to-back by index, then markers. Do not attempt to sort by z; it will do nothing.

- [ ] **Step 1: Create `src/gfx/LevelRenderer.hpp`**

```cpp
#pragma once

#include <SDL3/SDL_gpu.h>

#include <glm/vec4.hpp>

#include "logic/Level.hpp"

namespace horde::gfx {

class SpriteBatch;

// Atlas cells, in the 4x4 grid laid out by tools/make_placeholder_atlas.py.
glm::vec4 cellSquare();   // (0,0) bordered square
glm::vec4 cellDisc();     // (1,0) filled disc
glm::vec4 cellTriangle(); // (2,0) isoceles triangle, apex toward -y
glm::vec4 cellSolid();    // (1,1) solid fill

// Converts a stored colour to the linear float vector the GPU wants, opaque.
glm::vec4 toFloatColor(logic::Rgb color);

// Queues a whole level: background, then walls in list order, then markers.
//
// Shared by the editor and by LevelScene so the two views of a level cannot
// drift apart. Draw order is submission order — there is no depth test.
void renderLevel(const logic::Level& level, SpriteBatch& batch, SDL_GPUTexture* atlas);

// Queues one wall. Exposed separately so the editor can draw previews and
// highlights with a modified colour without duplicating the per-kind logic.
void renderWall(const logic::Wall& wall, SpriteBatch& batch, SDL_GPUTexture* atlas, glm::vec4 color);

} // namespace horde::gfx
```

- [ ] **Step 2: Create `src/gfx/LevelRenderer.cpp`**

```cpp
#include "gfx/LevelRenderer.hpp"

#include <cmath>

#include "gfx/SpriteBatch.hpp"
#include "logic/LevelGeometry.hpp"

namespace horde::gfx {

glm::vec4 cellSquare() {
    return atlasCell(0, 0, 4, 4);
}

glm::vec4 cellDisc() {
    return atlasCell(1, 0, 4, 4);
}

glm::vec4 cellTriangle() {
    return atlasCell(2, 0, 4, 4);
}

glm::vec4 cellSolid() {
    return atlasCell(1, 1, 4, 4);
}

glm::vec4 toFloatColor(logic::Rgb color) {
    return {static_cast<float>(color.r) / 255.0f, static_cast<float>(color.g) / 255.0f,
            static_cast<float>(color.b) / 255.0f, 1.0f};
}

void renderWall(const logic::Wall& wall, SpriteBatch& batch, SDL_GPUTexture* atlas, glm::vec4 color) {
    if (const auto* rect = std::get_if<logic::RectangleShape>(&wall.shape)) {
        drawCentered(batch, atlas, wall.center, rect->halfExtents * 2.0f, wall.rotation, cellSolid(), color);
        return;
    }

    if (const auto* tri = std::get_if<logic::TriangleShape>(&wall.shape)) {
        drawCentered(batch, atlas, wall.center, tri->halfExtents * 2.0f, wall.rotation, cellTriangle(), color);
        return;
    }

    if (const auto* circle = std::get_if<logic::CircleShape>(&wall.shape)) {
        const float diameter = circle->radius * 2.0f;
        drawCentered(batch, atlas, wall.center, {diameter, diameter}, 0.0f, cellDisc(), color);
        return;
    }

    // A polyline is one thin quad per segment, each centred on its segment's
    // midpoint and rotated to its direction. Points are in local space, so
    // transform each into world space first.
    const auto& line = std::get<logic::PolylineShape>(wall.shape);
    for (std::size_t i = 0; i + 1 < line.points.size(); ++i) {
        const glm::vec2 a = logic::localToWorld(wall, line.points[i]);
        const glm::vec2 b = logic::localToWorld(wall, line.points[i + 1]);
        const glm::vec2 delta = b - a;
        const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        if (length <= 0.0f) {
            continue;
        }

        drawCentered(batch, atlas, (a + b) * 0.5f, {length, line.thickness}, std::atan2(delta.y, delta.x),
                     cellSolid(), color);
    }
}

void renderLevel(const logic::Level& level, SpriteBatch& batch, SDL_GPUTexture* atlas) {
    // Background: exactly the bounds, drawn first so everything lands on top.
    drawCentered(batch, atlas, level.size * 0.5f, level.size, 0.0f, cellSolid(), toFloatColor(level.backgroundColor));

    for (const logic::Wall& wall : level.walls) {
        renderWall(wall, batch, atlas, toFloatColor(wall.color));
    }

    for (const logic::Marker& marker : level.markers) {
        glm::vec2 center{};
        glm::vec2 size{};
        logic::markerRect(level, marker, center, size);

        // Spawns green, exits red. Fixed, not authored: markers are placeholders
        // for a future integration rather than art.
        const glm::vec4 color = marker.kind == logic::MarkerKind::Spawn ? glm::vec4{0.25f, 0.85f, 0.35f, 1.0f}
                                                                       : glm::vec4{0.9f, 0.3f, 0.25f, 1.0f};
        drawCentered(batch, atlas, center, size, 0.0f, cellSolid(), color);
    }
}

} // namespace horde::gfx
```

- [ ] **Step 3: Rework `src/scene/LevelScene.hpp`**

Replace the private section, and add a constructor taking a level path:

```cpp
class LevelScene : public Scene {
public:
    LevelScene() = default;
    explicit LevelScene(std::filesystem::path levelPath) : m_levelPath(std::move(levelPath)) {}

    bool onEnter(Services& services) override;
    bool handleEvent(const SDL_Event& event) override;
    void update(float dt) override;
    void render(gfx::SpriteBatch& batch) override;
    void debugUi() override;
    void onResize(float width, float height) override;

    gfx::Camera2D& camera() override {
        return m_camera;
    }

    const char* name() const override {
        return "Level";
    }

private:
    Services* m_services = nullptr;
    gfx::Camera2D m_camera;
    gfx::CameraController m_cameraController;

    std::filesystem::path m_levelPath;
    logic::Level m_level;

    static constexpr size_t MaxUnits = 100000;
    int enemy_size = 5;
    UnitManager unit_manager{MaxUnits, glm::vec2{600.0f, 400.0f}, enemy_size};
};
```

Add `#include <filesystem>` and `#include "logic/Level.hpp"` to the header's includes. Note `MaxUnits` becomes `static constexpr` (it was a mutable member used as a constructor argument, which is fragile) and drops from 1,000,000 to 100,000 so the editor work is not competing with a million-unit spawn loop every time you press Play.

- [ ] **Step 4: Rework `LevelScene::onEnter` and `render` in `src/scene/LevelScene.cpp`**

```cpp
bool LevelScene::onEnter(Services& services) {
    m_services = &services;

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(services.window, &width, &height);
    m_camera.setViewport(static_cast<float>(width), static_cast<float>(height));

    // A missing or malformed level must never stop the game booting: fall back
    // to a bare default rather than failing to enter the scene.
    const std::filesystem::path path =
        m_levelPath.empty() ? paths::asset("levels/default.level.json") : m_levelPath;

    std::string error;
    if (std::optional<logic::Level> loaded = logic::loadLevel(path, &error)) {
        m_level = std::move(*loaded);
    } else {
        SDL_Log("LevelScene: falling back to an empty level (%s)", error.c_str());
        m_level = logic::makeDefaultLevel();
    }

    unit_manager.SetWorldBounds(m_level.size);

    for (size_t i = 0; i < MaxUnits; ++i) {
        unit_manager.SpawnUnit(
            // silly wrapping
            glm::vec2(enemy_size * (i * enemy_size) / int(m_level.size.y), (i * enemy_size) % int(m_level.size.y)),
            glm::vec2(20, (i * enemy_size) % int(m_level.size.y)), 10);
    }

    return true;
}

void LevelScene::render(gfx::SpriteBatch& batch) {
    gfx::renderLevel(m_level, batch, m_services->atlas->handle());

    gfx::Sprite unit;
    unit.size = {enemy_size, enemy_size};
    unit.uv = gfx::atlasCell(1, 0, 4, 4);
    // TODO: color could be determined from hp
    unit.color = {0.0f, 1.0f, 0.2f, 1.0f};

    const size_t unitCount = unit_manager.GetCurrentUnits();
    const glm::vec2* positions = unit_manager.GetPositionsPtr();

    for (size_t i = 0; i < unitCount; ++i) {
        unit.position = {positions[i], 0.0f};
        batch.draw(unit, m_services->atlas->handle());
    }
}
```

Add these includes to `LevelScene.cpp`: `<optional>`, `<string>`, `"core/Paths.hpp"`, `"gfx/LevelRenderer.hpp"`, `"logic/LevelIO.hpp"`.

- [ ] **Step 5: Register `LevelRenderer.cpp` in `CMakeLists.txt`**

Add `src/gfx/LevelRenderer.cpp` to `add_executable`, next to the other `src/gfx/` entries.

- [ ] **Step 6: Build and verify the level renders**

Run:
```bash
clang-format -i src/gfx/LevelRenderer.hpp src/gfx/LevelRenderer.cpp src/scene/LevelScene.hpp src/scene/LevelScene.cpp
cmake --build --preset linux-debug 2>&1 | tail -20
./build/linux-debug/bin/horde
```
Click **Play**. Expected, on a dark blue-grey background exactly 600x400:
- a red horizontal bar upper-left, and an orange bar lower-right **tilted 30 degrees clockwise** (clockwise, not anticlockwise — this confirms the y-flip convention);
- a green triangle in the middle with its **apex pointing up**;
- a blue circle upper-right with a **smooth, not stair-stepped** edge;
- a yellow zig-zag polyline lower-left, four points and three segments, unbroken at the joints;
- a green vertical band on the **left** edge and a red one on the **right** edge, both vertically centred.

If the triangle points down, `in_triangle` in Task 1 or the apex convention is wrong. If the orange bar tilts anticlockwise, a rotation sign is inverted. Both are worth stopping to fix here rather than later.

Also confirm the fallback: `mv assets/levels/default.level.json /tmp/` (note: the executable reads its *own* copy under `build/linux-debug/bin/assets/`, so move that one), run again, and check the game still enters the level, logs the fallback message, and shows an empty arena rather than crashing. Restore the file and rebuild afterwards.

- [ ] **Step 7: Commit**

```bash
git add src/gfx/LevelRenderer.hpp src/gfx/LevelRenderer.cpp src/scene/LevelScene.hpp src/scene/LevelScene.cpp CMakeLists.txt
git commit -m "Render levels, and load one in LevelScene

Adds gfx::renderLevel, shared by LevelScene and (later) the editor so the
two views of a level cannot drift apart. Draw order is submission order:
the sprite pipeline sets no depth-stencil state, so Sprite::position.z
does not order anything.

LevelScene now loads assets/levels/default.level.json and falls back to an
empty default if it is missing or malformed, so a broken level file can
never stop the game booting."
```

---

## Task 5: The editor scene shell

An editor you can open, pan around, and leave. No editing yet — this is the frame everything else hangs on.

**Files:**
- Create: `src/editor/UndoStack.hpp`, `src/editor/EditorState.hpp`
- Create: `src/scene/EditorScene.hpp`, `src/scene/EditorScene.cpp`
- Create: `src/editor/EditorUi.hpp`, `src/editor/EditorUi.cpp`
- Modify: `src/scene/MainMenuScene.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `logic::makeDefaultLevel` (Task 2), `gfx::renderLevel` (Task 4).
- Produces: `editor::Tool`, `editor::Selection`, `editor::SnapSettings`, `editor::EditorState`, `editor::UndoStack`, `editor::drawEditorUi(EditorState&, bool& wantsExit)`, `scene::EditorScene`.

**Critical input note:** `gfx::CameraController::handleEvent` (`src/gfx/Camera2D.cpp:56-60`) pans on **left OR middle** drag. In the editor the left button belongs to selection and placement, so `EditorScene` must forward **only** wheel and middle-button events to the controller. Do not change `CameraController` — `LevelScene` and `TechTreeScene` rely on its current behaviour.

- [ ] **Step 1: Create `src/editor/UndoStack.hpp`**

```cpp
#pragma once

#include <utility>
#include <vector>

#include "logic/Level.hpp"

namespace horde::editor {

// Undo as whole-Level snapshots.
//
// Levels hold tens of walls, so copying one is trivially cheap — far cheaper
// than the per-command inverse logic a command pattern would need, and much
// harder to get subtly wrong. Push a snapshot BEFORE each mutation.
class UndoStack {
public:
    static constexpr std::size_t kMaxDepth = 64;

    // Records the state before a mutation. Discards any redo history, since the
    // user has now taken a different branch.
    void push(const logic::Level& level) {
        m_past.push_back(level);
        if (m_past.size() > kMaxDepth) {
            m_past.erase(m_past.begin());
        }
        m_future.clear();
    }

    bool undo(logic::Level& level) {
        if (m_past.empty()) {
            return false;
        }
        m_future.push_back(level);
        level = std::move(m_past.back());
        m_past.pop_back();
        return true;
    }

    bool redo(logic::Level& level) {
        if (m_future.empty()) {
            return false;
        }
        m_past.push_back(level);
        level = std::move(m_future.back());
        m_future.pop_back();
        return true;
    }

    bool canUndo() const {
        return !m_past.empty();
    }

    bool canRedo() const {
        return !m_future.empty();
    }

    void clear() {
        m_past.clear();
        m_future.clear();
    }

private:
    std::vector<logic::Level> m_past;
    std::vector<logic::Level> m_future;
};

} // namespace horde::editor
```

- [ ] **Step 2: Create `src/editor/EditorState.hpp`**

```cpp
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include "editor/UndoStack.hpp"
#include "logic/Level.hpp"

namespace horde::editor {

// Which tool the left mouse button is currently driving.
enum class Tool { Select, Rectangle, Triangle, Circle, Polyline, Spawn, Exit };

enum class SelectionKind { None, Wall, Marker };

// What is selected or hovered. `index` indexes Level::walls or Level::markers
// depending on `kind`, and is meaningless when kind is None.
struct Selection {
    SelectionKind kind = SelectionKind::None;
    std::size_t index = 0;

    bool isWall() const {
        return kind == SelectionKind::Wall;
    }

    bool isMarker() const {
        return kind == SelectionKind::Marker;
    }

    void clear() {
        kind = SelectionKind::None;
        index = 0;
    }

    bool operator==(const Selection& other) const {
        return kind == other.kind && (kind == SelectionKind::None || index == other.index);
    }
};

// Both toggles default off: off-grid, off-axis placement is the norm and
// snapping is the opt-in aid.
struct SnapSettings {
    bool position = false;
    bool rotation = false;
    float gridSize = 10.0f;
    float rotationDegrees = 15.0f;
};

// Everything the editor is currently doing. Passed by reference to the UI, the
// tools and the handles, so none of them own any of it.
struct EditorState {
    logic::Level level = logic::makeDefaultLevel();

    Selection selection;
    Selection hovered;

    Tool tool = Tool::Select;
    SnapSettings snap;

    // Colour applied to the next wall placed.
    logic::Rgb newWallColor{180, 180, 190};

    // Empty until the level has been saved or loaded from somewhere.
    std::filesystem::path path;
    bool dirty = false;

    // Last save or load outcome, shown in the UI. Empty when there is nothing
    // to report.
    std::string status;

    UndoStack undo;

    // Snapshots the level so the next mutation can be undone. Call immediately
    // BEFORE mutating, once per user-visible operation — at the start of a drag,
    // not once per frame of it.
    void beginMutation() {
        undo.push(level);
        dirty = true;
    }
};

} // namespace horde::editor
```

- [ ] **Step 3: Create `src/editor/EditorUi.hpp`**

Later tasks add panels to this file; the signature does not change.

```cpp
#pragma once

namespace horde::editor {

struct EditorState;

// Draws every ImGui panel for the editor and applies whatever the user did to
// `state`. Sets `wantsExit` when the user asks to return to the menu.
void drawEditorUi(EditorState& state, bool& wantsExit);

} // namespace horde::editor
```

- [ ] **Step 4: Create `src/editor/EditorUi.cpp` with the level panel**

```cpp
#include "editor/EditorUi.hpp"

#include <imgui.h>

#include "editor/EditorState.hpp"

namespace horde::editor {
namespace {

void drawLevelPanel(EditorState& state, bool& wantsExit) {
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280.0f, 260.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Level");

    if (ImGui::Button("Back to menu", ImVec2(160.0f, 0.0f))) {
        wantsExit = true;
    }

    ImGui::SeparatorText("Bounds");

    float size[2] = {state.level.size.x, state.level.size.y};
    if (ImGui::DragFloat2("Size", size, 1.0f, 50.0f, 10000.0f, "%.0f")) {
        state.beginMutation();
        state.level.size = {size[0], size[1]};
    }

    ImGui::SeparatorText("Background");

    float background[3] = {static_cast<float>(state.level.backgroundColor.r) / 255.0f,
                           static_cast<float>(state.level.backgroundColor.g) / 255.0f,
                           static_cast<float>(state.level.backgroundColor.b) / 255.0f};
    if (ImGui::ColorEdit3("Colour", background)) {
        state.beginMutation();
        state.level.backgroundColor = {static_cast<std::uint8_t>(background[0] * 255.0f),
                                       static_cast<std::uint8_t>(background[1] * 255.0f),
                                       static_cast<std::uint8_t>(background[2] * 255.0f)};
    }

    ImGui::SeparatorText("History");

    ImGui::BeginDisabled(!state.undo.canUndo());
    if (ImGui::Button("Undo")) {
        state.undo.undo(state.level);
        state.selection.clear();
        state.dirty = true;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    ImGui::BeginDisabled(!state.undo.canRedo());
    if (ImGui::Button("Redo")) {
        state.undo.redo(state.level);
        state.selection.clear();
        state.dirty = true;
    }
    ImGui::EndDisabled();

    if (!state.status.empty()) {
        ImGui::SeparatorText("Status");
        ImGui::TextWrapped("%s", state.status.c_str());
    }

    ImGui::End();
}

} // namespace

void drawEditorUi(EditorState& state, bool& wantsExit) {
    drawLevelPanel(state, wantsExit);
}

} // namespace horde::editor
```

`beginMutation` on every drag frame would flood the undo stack — a known wart of `DragFloat`, and acceptable here because size and colour edits are rare. Later tasks use the same pattern for in-world drags but snapshot only on the mouse-down that starts the drag.

- [ ] **Step 5: Create `src/scene/EditorScene.hpp`**

```cpp
#pragma once

#include "editor/EditorState.hpp"
#include "gfx/Camera2D.hpp"
#include "scene/Scene.hpp"

namespace horde::scene {

// The level editor.
//
// Dear ImGui drives its panels, which is consistent with the project's
// "ImGui is for tooling" rule: a level editor is tooling. Direct manipulation
// happens in world space through the shared Camera2D.
class EditorScene : public Scene {
public:
    bool onEnter(Services& services) override;
    bool handleEvent(const SDL_Event& event) override;
    void render(gfx::SpriteBatch& batch) override;
    void debugUi() override;
    void onResize(float width, float height) override;

    gfx::Camera2D& camera() override {
        return m_camera;
    }

    const char* name() const override {
        return "Editor";
    }

private:
    // World-space position of the mouse, updated every motion event.
    glm::vec2 mouseWorld(float screenX, float screenY) const;

    Services* m_services = nullptr;
    gfx::Camera2D m_camera;
    gfx::CameraController m_cameraController;

    editor::EditorState m_state;
    glm::vec2 m_cursorWorld{0.0f, 0.0f};
};

} // namespace horde::scene
```

- [ ] **Step 6: Create `src/scene/EditorScene.cpp`**

```cpp
#include "scene/EditorScene.hpp"

#include <imgui.h>

#include "editor/EditorUi.hpp"
#include "gfx/LevelRenderer.hpp"
#include "gfx/SpriteBatch.hpp"
#include "gfx/Texture.hpp"
#include "scene/SceneStack.hpp"

namespace horde::scene {

bool EditorScene::onEnter(Services& services) {
    m_services = &services;

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(services.window, &width, &height);
    m_camera.setViewport(static_cast<float>(width), static_cast<float>(height));

    // Frame the whole level with a margin, so a new editor opens on something
    // sensible rather than on the origin.
    m_camera.setCenter(m_state.level.size * 0.5f);
    const float fit = std::min(static_cast<float>(width) / (m_state.level.size.x * 1.2f),
                               static_cast<float>(height) / (m_state.level.size.y * 1.2f));
    m_camera.setZoom(fit);

    return true;
}

glm::vec2 EditorScene::mouseWorld(float screenX, float screenY) const {
    return m_camera.screenToWorld({screenX, screenY});
}

bool EditorScene::handleEvent(const SDL_Event& event) {
    // ImGui gets first refusal on the mouse, so dragging a panel never also
    // drags the world behind it.
    const ImGuiIO& io = ImGui::GetIO();

    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        m_cursorWorld = mouseWorld(event.motion.x, event.motion.y);
    }

    // CameraController pans on left OR middle drag, but in the editor the left
    // button belongs to the tools. Forward only wheel and middle-button events.
    const bool cameraEvent =
        event.type == SDL_EVENT_MOUSE_WHEEL ||
        ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
         event.button.button == SDL_BUTTON_MIDDLE) ||
        (event.type == SDL_EVENT_MOUSE_MOTION && (event.motion.state & SDL_BUTTON_MMASK) != 0);

    if (cameraEvent && !io.WantCaptureMouse) {
        return m_cameraController.handleEvent(event, m_camera);
    }

    return false;
}

void EditorScene::render(gfx::SpriteBatch& batch) {
    gfx::renderLevel(m_state.level, batch, m_services->atlas->handle());
}

void EditorScene::debugUi() {
    bool wantsExit = false;
    editor::drawEditorUi(m_state, wantsExit);

    if (wantsExit) {
        m_services->scenes->pop();
    }
}

void EditorScene::onResize(float width, float height) {
    m_camera.setViewport(width, height);
}

} // namespace horde::scene
```

Add `#include <algorithm>` for `std::min`.

- [ ] **Step 7: Add the menu button**

In `src/scene/MainMenuScene.cpp`, add `#include "scene/EditorScene.hpp"` and insert after the Upgrades button:

```cpp
    if (ImGui::Button("Level Editor", ImVec2(160.0f, 0.0f))) {
        m_services->scenes->push(std::make_unique<EditorScene>());
    }
```

- [ ] **Step 8: Register sources and build**

Add `src/scene/EditorScene.cpp` and `src/editor/EditorUi.cpp` to `add_executable` in `CMakeLists.txt`.

```bash
clang-format -i src/editor/*.hpp src/editor/*.cpp src/scene/EditorScene.hpp src/scene/EditorScene.cpp src/scene/MainMenuScene.cpp
cmake --build --preset linux-debug 2>&1 | tail -20
./build/linux-debug/bin/horde
```
Expected: a third **Level Editor** button on the menu. Clicking it shows a dark background rectangle framed in the window, with a green band on its west edge and a red band on its east. Middle-drag pans; the wheel zooms toward the cursor; **left-drag does nothing** (this is the check that Step 6's event filter works). Changing Size in the Level panel resizes the background and moves both markers with it. Changing the background colour works, and Undo reverts it. Back to menu returns.

- [ ] **Step 9: Commit**

```bash
git add src/editor/ src/scene/EditorScene.hpp src/scene/EditorScene.cpp src/scene/MainMenuScene.cpp CMakeLists.txt
git commit -m "Add the level editor scene shell

An EditorScene reachable from the main menu, with a snapshot-based undo
stack, an editor state object shared by the UI and (later) the tools, and
a level panel for bounds and background colour.

The editor forwards only wheel and middle-button events to the shared
CameraController, because that controller pans on left-drag too and the
left button belongs to the editor's tools."
```

---

## Task 6: Selection, hover highlight and drag-to-move

**Files:**
- Create: `src/editor/HitTest.hpp`, `src/editor/HitTest.cpp`
- Modify: `src/scene/EditorScene.hpp`, `src/scene/EditorScene.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `logic::worldToLocal`, `logic::wallAabb`, `logic::markerRect` (Task 2), `EditorState` (Task 5).
- Produces: `editor::hitTestWall(const logic::Wall&, glm::vec2) -> bool`, `editor::pick(const logic::Level&, glm::vec2) -> Selection`.

- [ ] **Step 1: Create `src/editor/HitTest.hpp`**

```cpp
#pragma once

#include <glm/vec2.hpp>

#include "editor/EditorState.hpp"
#include "logic/Level.hpp"

namespace horde::editor {

// True if `world` lies inside this wall.
//
// Works by transforming the point into the wall's local space, where the shape
// is axis aligned and each kind's test is a two-line formula. This is what makes
// picking correct at arbitrary rotations.
bool hitTestWall(const logic::Wall& wall, glm::vec2 world);

// True if `world` lies inside this marker's band.
bool hitTestMarker(const logic::Level& level, const logic::Marker& marker, glm::vec2 world);

// What the cursor is over: the topmost wall (highest index, since a later index
// draws on top), else a marker, else nothing.
Selection pick(const logic::Level& level, glm::vec2 world);

} // namespace horde::editor
```

- [ ] **Step 2: Create `src/editor/HitTest.cpp`**

```cpp
#include "editor/HitTest.hpp"

#include <cmath>

#include "logic/LevelGeometry.hpp"

namespace horde::editor {
namespace {

// Shortest distance from `p` to the segment ab.
float distanceToSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 ab = b - a;
    const float lengthSquared = ab.x * ab.x + ab.y * ab.y;
    if (lengthSquared <= 0.0f) {
        const glm::vec2 d = p - a;
        return std::sqrt(d.x * d.x + d.y * d.y);
    }

    float t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lengthSquared;
    t = std::clamp(t, 0.0f, 1.0f);

    const glm::vec2 d = p - (a + ab * t);
    return std::sqrt(d.x * d.x + d.y * d.y);
}

} // namespace

bool hitTestWall(const logic::Wall& wall, glm::vec2 world) {
    const glm::vec2 local = logic::worldToLocal(wall, world);

    if (const auto* rect = std::get_if<logic::RectangleShape>(&wall.shape)) {
        return std::abs(local.x) <= rect->halfExtents.x && std::abs(local.y) <= rect->halfExtents.y;
    }

    if (const auto* tri = std::get_if<logic::TriangleShape>(&wall.shape)) {
        // Apex at (0, -hy), base from (-hx, +hy) to (+hx, +hy). The half-width
        // grows linearly from 0 at the apex to hx at the base, which mirrors
        // exactly how the atlas cell is drawn.
        if (local.y < -tri->halfExtents.y || local.y > tri->halfExtents.y) {
            return false;
        }
        const float t = (local.y + tri->halfExtents.y) / (tri->halfExtents.y * 2.0f);
        return std::abs(local.x) <= tri->halfExtents.x * t;
    }

    if (const auto* circle = std::get_if<logic::CircleShape>(&wall.shape)) {
        return local.x * local.x + local.y * local.y <= circle->radius * circle->radius;
    }

    const auto& line = std::get<logic::PolylineShape>(wall.shape);
    const float reach = std::max(line.thickness * 0.5f, 3.0f); // never harder to click than 3 units
    for (std::size_t i = 0; i + 1 < line.points.size(); ++i) {
        if (distanceToSegment(local, line.points[i], line.points[i + 1]) <= reach) {
            return true;
        }
    }
    return false;
}

bool hitTestMarker(const logic::Level& level, const logic::Marker& marker, glm::vec2 world) {
    glm::vec2 center{};
    glm::vec2 size{};
    logic::markerRect(level, marker, center, size);

    return std::abs(world.x - center.x) <= size.x * 0.5f && std::abs(world.y - center.y) <= size.y * 0.5f;
}

Selection pick(const logic::Level& level, glm::vec2 world) {
    // Walls first, topmost down, so what you click is what you see on top.
    for (std::size_t i = level.walls.size(); i-- > 0;) {
        if (hitTestWall(level.walls[i], world)) {
            return Selection{SelectionKind::Wall, i};
        }
    }

    for (std::size_t i = level.markers.size(); i-- > 0;) {
        if (hitTestMarker(level, level.markers[i], world)) {
            return Selection{SelectionKind::Marker, i};
        }
    }

    return Selection{};
}

} // namespace horde::editor
```

Add `#include <algorithm>` for `std::clamp` and `std::max`.

- [ ] **Step 3: Add drag state to `EditorScene.hpp`**

Add these private members:

```cpp
    // Set while the left button is held after grabbing a selected element.
    bool m_draggingBody = false;
    glm::vec2 m_dragGrabOffset{0.0f, 0.0f};
```

- [ ] **Step 4: Handle selection and body dragging in `EditorScene::handleEvent`**

Insert this before the `cameraEvent` block, so tool input takes priority:

```cpp
    if (!io.WantCaptureMouse && m_state.tool == editor::Tool::Select) {
        switch (event.type) {
            case SDL_EVENT_MOUSE_MOTION: {
                if (m_draggingBody && m_state.selection.isWall()) {
                    m_state.level.walls[m_state.selection.index].center = m_cursorWorld - m_dragGrabOffset;
                    return true;
                }
                m_state.hovered = editor::pick(m_state.level, m_cursorWorld);
                return false;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button != SDL_BUTTON_LEFT) {
                    break;
                }
                m_state.selection = editor::pick(m_state.level, m_cursorWorld);
                if (m_state.selection.isWall()) {
                    // Snapshot once, here, at the start of the drag — not on
                    // every motion event.
                    m_state.beginMutation();
                    m_draggingBody = true;
                    m_dragGrabOffset = m_cursorWorld - m_state.level.walls[m_state.selection.index].center;
                }
                return true;
            }

            case SDL_EVENT_MOUSE_BUTTON_UP: {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    m_draggingBody = false;
                }
                break;
            }

            default:
                break;
        }
    }
```

Marker dragging is deliberately absent here; markers move along their edge only and get their own handling in Task 12.

- [ ] **Step 5: Draw the hover and selection highlight in `EditorScene::render`**

Replace the body of `render`:

```cpp
void EditorScene::render(gfx::SpriteBatch& batch) {
    SDL_GPUTexture* atlas = m_services->atlas->handle();
    gfx::renderLevel(m_state.level, batch, atlas);

    // Redraw the hovered and selected walls tinted, on top of the level. There
    // is no depth test, so drawing after is what puts them on top.
    const auto highlight = [&](const editor::Selection& selection, glm::vec4 tint) {
        if (selection.isWall() && selection.index < m_state.level.walls.size()) {
            gfx::renderWall(m_state.level.walls[selection.index], batch, atlas, tint);
        }
    };

    if (!(m_state.hovered == m_state.selection)) {
        highlight(m_state.hovered, {1.0f, 1.0f, 1.0f, 0.25f});
    }
    highlight(m_state.selection, {1.0f, 0.85f, 0.2f, 0.45f});
}
```

- [ ] **Step 6: Register, build and verify**

Add `src/editor/HitTest.cpp` to `CMakeLists.txt`, then:

```bash
clang-format -i src/editor/HitTest.hpp src/editor/HitTest.cpp src/scene/EditorScene.hpp src/scene/EditorScene.cpp
cmake --build --preset linux-debug 2>&1 | tail -20
./build/linux-debug/bin/horde
```

The editor still opens on an empty default level, so to check this, temporarily open the default level by changing `EditorScene::onEnter` to load `paths::asset("levels/default.level.json")` — **or** skip ahead and verify after Task 8 gives you placement. If you do the temporary load, expect: hovering any of the five walls tints it faintly white; clicking one tints it yellow; dragging moves it, including **the rotated orange bar and the triangle, which must highlight only when the cursor is genuinely inside their rotated outline, not their bounding box**. Undo restores a moved wall's position in one step, not many. Revert the temporary load before committing.

- [ ] **Step 7: Commit**

```bash
git add src/editor/HitTest.hpp src/editor/HitTest.cpp src/scene/EditorScene.hpp src/scene/EditorScene.cpp CMakeLists.txt
git commit -m "Add wall picking, hover highlight and drag-to-move

Hit-testing transforms the cursor into each wall's local space, where the
shape is axis aligned, so picking stays correct at arbitrary rotations.
Picking walks walls topmost-first so what you click is what you see.

A drag snapshots the level once on mouse-down rather than per motion
event, so one drag is one undo step."
```

---

## Task 7: The toolbar and placing rectangles, triangles and circles

**Files:**
- Create: `src/editor/Tools.hpp`, `src/editor/Tools.cpp`
- Modify: `src/editor/EditorUi.cpp` (toolbar panel)
- Modify: `src/scene/EditorScene.hpp`, `src/scene/EditorScene.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `EditorState`, `Tool` (Task 5), `gfx::renderWall` (Task 4).
- Produces: `editor::Placement`, `editor::isBoxTool(Tool) -> bool`, `editor::makeWallFromDrag(Tool, glm::vec2 start, glm::vec2 end, logic::Rgb, logic::Wall& out) -> bool`, `editor::toolName(Tool) -> const char*`.

- [ ] **Step 1: Create `src/editor/Tools.hpp`**

```cpp
#pragma once

#include <glm/vec2.hpp>

#include "editor/EditorState.hpp"
#include "logic/Level.hpp"

namespace horde::editor {

// A drag smaller than this in either axis is treated as a mis-click rather than
// a wall, which is what stops the editor producing degenerate geometry that
// validation would then refuse to save.
inline constexpr float kMinimumDragExtent = 4.0f;

// A click-drag in progress. `start` is where the button went down, `current`
// follows the cursor.
struct Placement {
    bool active = false;
    glm::vec2 start{0.0f, 0.0f};
    glm::vec2 current{0.0f, 0.0f};
};

// True for the tools whose placement gesture sweeps a bounding box: rectangle,
// triangle and circle. Polyline and the two marker tools do not.
bool isBoxTool(Tool tool);

const char* toolName(Tool tool);

// Builds a wall from a swept box. Returns false if the drag was too small to be
// a deliberate placement, or if `tool` is not a box tool.
//
// A circle takes its radius from half the SMALLER of the box's two extents, so
// that it always fits inside the box the user swept.
bool makeWallFromDrag(Tool tool, glm::vec2 start, glm::vec2 end, logic::Rgb color, logic::Wall& out);

} // namespace horde::editor
```

- [ ] **Step 2: Create `src/editor/Tools.cpp`**

```cpp
#include "editor/Tools.hpp"

#include <algorithm>
#include <cmath>

namespace horde::editor {

bool isBoxTool(Tool tool) {
    return tool == Tool::Rectangle || tool == Tool::Triangle || tool == Tool::Circle;
}

const char* toolName(Tool tool) {
    switch (tool) {
        case Tool::Select:    return "Select";
        case Tool::Rectangle: return "Rectangle";
        case Tool::Triangle:  return "Triangle";
        case Tool::Circle:    return "Circle";
        case Tool::Polyline:  return "Polyline";
        case Tool::Spawn:     return "Spawn";
        case Tool::Exit:      return "Exit";
    }
    return "Select";
}

bool makeWallFromDrag(Tool tool, glm::vec2 start, glm::vec2 end, logic::Rgb color, logic::Wall& out) {
    if (!isBoxTool(tool)) {
        return false;
    }

    const glm::vec2 extent{std::abs(end.x - start.x), std::abs(end.y - start.y)};
    if (extent.x < kMinimumDragExtent || extent.y < kMinimumDragExtent) {
        return false;
    }

    out = logic::Wall{};
    out.center = (start + end) * 0.5f;
    out.rotation = 0.0f;
    out.color = color;

    switch (tool) {
        case Tool::Rectangle:
            out.shape = logic::RectangleShape{extent * 0.5f};
            break;
        case Tool::Triangle:
            out.shape = logic::TriangleShape{extent * 0.5f};
            break;
        case Tool::Circle:
            out.shape = logic::CircleShape{std::min(extent.x, extent.y) * 0.5f};
            break;
        default:
            return false;
    }

    return true;
}

} // namespace horde::editor
```

- [ ] **Step 3: Add the toolbar panel to `src/editor/EditorUi.cpp`**

Add `#include "editor/Tools.hpp"` and this function in the anonymous namespace, then call it from `drawEditorUi` before `drawLevelPanel`:

```cpp
void drawToolbar(EditorState& state) {
    ImGui::SetNextWindowPos(ImVec2(320.0f, 20.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(240.0f, 300.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Tools");

    const Tool tools[] = {Tool::Select,   Tool::Rectangle, Tool::Triangle, Tool::Circle,
                          Tool::Polyline, Tool::Spawn,     Tool::Exit};

    for (int i = 0; i < static_cast<int>(std::size(tools)); ++i) {
        const Tool tool = tools[i];
        char label[64];
        std::snprintf(label, sizeof(label), "%d  %s", i + 1, toolName(tool));

        if (ImGui::RadioButton(label, state.tool == tool)) {
            state.tool = tool;
        }
    }

    ImGui::SeparatorText("New wall colour");

    float color[3] = {static_cast<float>(state.newWallColor.r) / 255.0f,
                      static_cast<float>(state.newWallColor.g) / 255.0f,
                      static_cast<float>(state.newWallColor.b) / 255.0f};
    if (ImGui::ColorEdit3("##newcolor", color)) {
        state.newWallColor = {static_cast<std::uint8_t>(color[0] * 255.0f),
                              static_cast<std::uint8_t>(color[1] * 255.0f),
                              static_cast<std::uint8_t>(color[2] * 255.0f)};
    }

    ImGui::End();
}
```

Add `#include <cstdio>` and `#include <iterator>` for `std::snprintf` and `std::size`.

- [ ] **Step 4: Add placement to `EditorScene`**

In `EditorScene.hpp` add `#include "editor/Tools.hpp"` and the member `editor::Placement m_placement;`.

In `EditorScene::handleEvent`, add this block **before** the Select-tool block:

```cpp
    if (!io.WantCaptureMouse && editor::isBoxTool(m_state.tool)) {
        switch (event.type) {
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    m_placement.active = true;
                    m_placement.start = m_cursorWorld;
                    m_placement.current = m_cursorWorld;
                    return true;
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                m_placement.current = m_cursorWorld;
                return m_placement.active;

            case SDL_EVENT_MOUSE_BUTTON_UP: {
                if (event.button.button != SDL_BUTTON_LEFT || !m_placement.active) {
                    break;
                }
                m_placement.active = false;

                logic::Wall wall;
                if (editor::makeWallFromDrag(m_state.tool, m_placement.start, m_cursorWorld, m_state.newWallColor,
                                             wall)) {
                    m_state.beginMutation();
                    m_state.level.walls.push_back(std::move(wall));
                }
                // The tool stays armed: walls come in runs.
                return true;
            }

            default:
                break;
        }
    }
```

- [ ] **Step 5: Draw the translucent placement preview**

At the end of `EditorScene::render`:

```cpp
    // A translucent preview of what the current drag would produce, so the
    // result is visible before the button is released.
    if (m_placement.active) {
        logic::Wall preview;
        if (editor::makeWallFromDrag(m_state.tool, m_placement.start, m_placement.current, m_state.newWallColor,
                                     preview)) {
            glm::vec4 tint = gfx::toFloatColor(preview.color);
            tint.a = 0.45f;
            gfx::renderWall(preview, batch, atlas, tint);
        }
    }
```

- [ ] **Step 6: Add keyboard shortcuts**

At the top of `EditorScene::handleEvent`, after the ImGui IO fetch:

```cpp
    if (event.type == SDL_EVENT_KEY_DOWN && !io.WantCaptureKeyboard) {
        switch (event.key.key) {
            case SDLK_1: m_state.tool = editor::Tool::Select;    return true;
            case SDLK_2: m_state.tool = editor::Tool::Rectangle; return true;
            case SDLK_3: m_state.tool = editor::Tool::Triangle;  return true;
            case SDLK_4: m_state.tool = editor::Tool::Circle;    return true;
            case SDLK_5: m_state.tool = editor::Tool::Polyline;  return true;
            case SDLK_6: m_state.tool = editor::Tool::Spawn;     return true;
            case SDLK_7: m_state.tool = editor::Tool::Exit;      return true;

            case SDLK_ESCAPE:
                m_placement.active = false;
                m_state.tool = editor::Tool::Select;
                return true;

            case SDLK_Z:
                if ((event.key.mod & SDL_KMOD_CTRL) != 0) {
                    m_state.undo.undo(m_state.level);
                    m_state.selection.clear();
                    m_state.dirty = true;
                    return true;
                }
                break;

            case SDLK_Y:
                if ((event.key.mod & SDL_KMOD_CTRL) != 0) {
                    m_state.undo.redo(m_state.level);
                    m_state.selection.clear();
                    m_state.dirty = true;
                    return true;
                }
                break;

            default:
                break;
        }
    }
```

Add `#include <SDL3/SDL_keycode.h>` if it is not already reachable.

- [ ] **Step 7: Register, build, verify**

Add `src/editor/Tools.cpp` to `CMakeLists.txt`, then build and run.

Expected in the editor: a **Tools** panel with seven radio buttons. Pick Rectangle (or press 2), drag on the level — a translucent preview follows the drag and a solid rectangle appears on release. The tool stays armed, so a second drag makes a second rectangle. Triangle produces an **apex-up** triangle; Circle produces a circle inscribed in the swept box. A tiny click-drag produces nothing. Press 1 or Escape to return to Select, then hover, click and drag the walls you just made. Ctrl+Z removes them one at a time.

- [ ] **Step 8: Commit**

```bash
git add src/editor/Tools.hpp src/editor/Tools.cpp src/editor/EditorUi.cpp src/scene/EditorScene.hpp src/scene/EditorScene.cpp CMakeLists.txt
git commit -m "Add the tool palette and box-swept wall placement

Rectangles, triangles and circles are placed by dragging out a bounding
box, with a translucent preview following the drag. The tool stays armed
after a placement because walls are placed in runs.

Drags below a minimum extent are discarded rather than producing
degenerate walls that validation would later refuse to save."
```

---

## Task 8: The inspector, the wall list, and deletion

**Files:**
- Modify: `src/editor/EditorUi.cpp` (two more panels)
- Modify: `src/scene/EditorScene.cpp` (Delete key)

**Interfaces:**
- Consumes: `EditorState`, `Selection` (Task 5), `logic::countMarkers` (Task 2).
- Produces: `editor::deleteSelected(EditorState&) -> bool` (declared in `EditorUi.hpp`).

- [ ] **Step 1: Declare deletion in `src/editor/EditorUi.hpp`**

```cpp
// Deletes whatever is selected, snapshotting first. Refuses, returning false,
// when the selection is the last spawn or the last exit: every level keeps at
// least one of each at all times.
bool deleteSelected(EditorState& state);
```

- [ ] **Step 2: Implement it in `src/editor/EditorUi.cpp`**

Outside the anonymous namespace:

```cpp
bool deleteSelected(EditorState& state) {
    if (state.selection.isWall() && state.selection.index < state.level.walls.size()) {
        state.beginMutation();
        state.level.walls.erase(state.level.walls.begin() + static_cast<std::ptrdiff_t>(state.selection.index));
        state.selection.clear();
        return true;
    }

    if (state.selection.isMarker() && state.selection.index < state.level.markers.size()) {
        const logic::MarkerKind kind = state.level.markers[state.selection.index].kind;
        if (logic::countMarkers(state.level, kind) <= 1) {
            state.status = "Every level needs at least one spawn and one exit.";
            return false;
        }
        state.beginMutation();
        state.level.markers.erase(state.level.markers.begin() + static_cast<std::ptrdiff_t>(state.selection.index));
        state.selection.clear();
        return true;
    }

    return false;
}
```

- [ ] **Step 3: Add the inspector panel**

In the anonymous namespace. This is the panel that gives numeric precision alongside Task 10's handles:

```cpp
void drawInspector(EditorState& state) {
    ImGui::SetNextWindowPos(ImVec2(20.0f, 300.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280.0f, 320.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Inspector");

    if (state.selection.kind == SelectionKind::None) {
        ImGui::TextDisabled("Nothing selected.");
        ImGui::End();
        return;
    }

    if (state.selection.isWall()) {
        logic::Wall& wall = state.level.walls[state.selection.index];

        ImGui::Text("Wall %zu", state.selection.index);
        ImGui::SeparatorText("Transform");

        float center[2] = {wall.center.x, wall.center.y};
        if (ImGui::DragFloat2("Centre", center, 0.5f)) {
            state.beginMutation();
            wall.center = {center[0], center[1]};
        }

        // Circles ignore rotation, so do not offer it for them.
        if (!std::holds_alternative<logic::CircleShape>(wall.shape)) {
            float degrees = glm::degrees(wall.rotation);
            if (ImGui::DragFloat("Rotation", &degrees, 1.0f, -360.0f, 360.0f, "%.1f deg")) {
                state.beginMutation();
                wall.rotation = glm::radians(degrees);
            }
            ImGui::TextDisabled("Positive rotation is clockwise on screen.");
        }

        ImGui::SeparatorText("Size");

        if (auto* rect = std::get_if<logic::RectangleShape>(&wall.shape)) {
            float extents[2] = {rect->halfExtents.x * 2.0f, rect->halfExtents.y * 2.0f};
            if (ImGui::DragFloat2("Width/Height", extents, 0.5f, 1.0f, 100000.0f)) {
                state.beginMutation();
                rect->halfExtents = {std::max(extents[0], 1.0f) * 0.5f, std::max(extents[1], 1.0f) * 0.5f};
            }
        } else if (auto* tri = std::get_if<logic::TriangleShape>(&wall.shape)) {
            float extents[2] = {tri->halfExtents.x * 2.0f, tri->halfExtents.y * 2.0f};
            if (ImGui::DragFloat2("Width/Height", extents, 0.5f, 1.0f, 100000.0f)) {
                state.beginMutation();
                tri->halfExtents = {std::max(extents[0], 1.0f) * 0.5f, std::max(extents[1], 1.0f) * 0.5f};
            }
        } else if (auto* circle = std::get_if<logic::CircleShape>(&wall.shape)) {
            float radius = circle->radius;
            if (ImGui::DragFloat("Radius", &radius, 0.5f, 1.0f, 100000.0f)) {
                state.beginMutation();
                circle->radius = std::max(radius, 1.0f);
            }
        } else {
            auto& line = std::get<logic::PolylineShape>(wall.shape);
            ImGui::Text("%zu points", line.points.size());
            float thickness = line.thickness;
            if (ImGui::DragFloat("Thickness", &thickness, 0.25f, 1.0f, 1000.0f)) {
                state.beginMutation();
                line.thickness = std::max(thickness, 1.0f);
            }
        }

        ImGui::SeparatorText("Colour");

        float color[3] = {static_cast<float>(wall.color.r) / 255.0f, static_cast<float>(wall.color.g) / 255.0f,
                          static_cast<float>(wall.color.b) / 255.0f};
        if (ImGui::ColorPicker3("##wallcolor", color, ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Uint8)) {
            state.beginMutation();
            wall.color = {static_cast<std::uint8_t>(color[0] * 255.0f), static_cast<std::uint8_t>(color[1] * 255.0f),
                          static_cast<std::uint8_t>(color[2] * 255.0f)};
        }
    } else {
        logic::Marker& marker = state.level.markers[state.selection.index];

        ImGui::Text("%s %zu", marker.kind == logic::MarkerKind::Spawn ? "Spawn" : "Exit", state.selection.index);
        ImGui::TextDisabled("Markers are placed on an edge and cannot be recoloured or rotated.");
        ImGui::Text("Edge: %s", marker.edge == logic::Edge::North   ? "north"
                                : marker.edge == logic::Edge::South ? "south"
                                : marker.edge == logic::Edge::East  ? "east"
                                                                    : "west");
        ImGui::Text("Offset: %.1f   Length: %.1f", marker.offset, marker.length);
    }

    ImGui::Separator();

    if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f))) {
        deleteSelected(state);
    }

    ImGui::End();
}
```

Add includes: `<glm/trigonometric.hpp>`, `<algorithm>`, `<variant>`.

- [ ] **Step 4: Add the wall list panel**

Selection is ambiguous when walls overlap; this makes it unambiguous.

```cpp
void drawWallList(EditorState& state) {
    ImGui::SetNextWindowPos(ImVec2(320.0f, 340.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(240.0f, 280.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Contents");

    ImGui::SeparatorText("Walls (last draws on top)");

    for (std::size_t i = 0; i < state.level.walls.size(); ++i) {
        const logic::Wall& wall = state.level.walls[i];
        const char* kind = std::holds_alternative<logic::RectangleShape>(wall.shape)  ? "Rectangle"
                           : std::holds_alternative<logic::TriangleShape>(wall.shape) ? "Triangle"
                           : std::holds_alternative<logic::CircleShape>(wall.shape)   ? "Circle"
                                                                                      : "Polyline";

        char label[64];
        std::snprintf(label, sizeof(label), "%zu  %s##wall%zu", i, kind, i);

        const bool selected = state.selection.isWall() && state.selection.index == i;
        if (ImGui::Selectable(label, selected)) {
            state.selection = Selection{SelectionKind::Wall, i};
        }
    }

    if (state.level.walls.empty()) {
        ImGui::TextDisabled("No walls yet.");
    }

    ImGui::SeparatorText("Markers");

    for (std::size_t i = 0; i < state.level.markers.size(); ++i) {
        const logic::Marker& marker = state.level.markers[i];
        char label[64];
        std::snprintf(label, sizeof(label), "%zu  %s##marker%zu", i,
                      marker.kind == logic::MarkerKind::Spawn ? "Spawn" : "Exit", i);

        const bool selected = state.selection.isMarker() && state.selection.index == i;
        if (ImGui::Selectable(label, selected)) {
            state.selection = Selection{SelectionKind::Marker, i};
        }
    }

    ImGui::End();
}
```

Call both from `drawEditorUi`, after `drawLevelPanel`.

- [ ] **Step 5: Wire the Delete key in `EditorScene::handleEvent`**

Add to the key switch, and `#include "editor/EditorUi.hpp"` if not already present:

```cpp
            case SDLK_DELETE:
            case SDLK_BACKSPACE:
                editor::deleteSelected(m_state);
                return true;
```

- [ ] **Step 6: Build and verify**

Expected: selecting a wall fills the Inspector with its centre, rotation, size and an RGB colour picker with a live preview swatch. Editing any field changes the wall in the view immediately. A circle shows Radius and **no Rotation control**. The Contents panel lists every wall and marker; clicking a row selects it and the world highlight follows. Delete removes the selected wall. Select a marker and press Delete — it refuses, and the Level panel's status line explains why, since there is only one of each.

- [ ] **Step 7: Commit**

```bash
git add src/editor/EditorUi.hpp src/editor/EditorUi.cpp src/scene/EditorScene.cpp
git commit -m "Add the inspector, contents list and deletion

The inspector gives exact numeric control over a wall's transform, size
and RGB colour, complementing the direct manipulation in the world. The
contents list makes selection unambiguous when walls overlap.

Deleting the last spawn or the last exit is refused: every level keeps at
least one of each at all times, which is why no validation rule needs to
check for their absence."
```

---

## Task 9: The polyline tool

**Files:**
- Modify: `src/editor/Tools.hpp`, `src/editor/Tools.cpp`
- Modify: `src/scene/EditorScene.hpp`, `src/scene/EditorScene.cpp`

**Interfaces:**
- Consumes: `Placement` (Task 7), `EditorState` (Task 5).
- Produces: `editor::PolylineDraft` and `editor::finishPolyline(const PolylineDraft&, logic::Rgb, float thickness, logic::Wall& out) -> bool`.

- [ ] **Step 1: Add `PolylineDraft` to `src/editor/Tools.hpp`**

```cpp
// A polyline being drawn. Points are in WORLD space while drafting; they are
// converted to the wall's local space when the draft is committed.
//
// Deliberately a separate type from Placement: a polyline is a click-per-point
// gesture, not a drag, and conflating the two would make both harder to follow.
struct PolylineDraft {
    bool active = false;
    std::vector<glm::vec2> points;
    glm::vec2 cursor{0.0f, 0.0f}; // where the next point would land

    void clear() {
        active = false;
        points.clear();
    }
};

// Commits a draft to a wall, converting its world-space points into local space
// around their centroid. Returns false if the draft has fewer than two points,
// which is not a polyline.
bool finishPolyline(const PolylineDraft& draft, logic::Rgb color, float thickness, logic::Wall& out);
```

Add `#include <vector>`.

- [ ] **Step 2: Implement `finishPolyline` in `src/editor/Tools.cpp`**

```cpp
bool finishPolyline(const PolylineDraft& draft, logic::Rgb color, float thickness, logic::Wall& out) {
    if (draft.points.size() < 2) {
        return false;
    }

    // The wall's centre is the centroid of its points, so that rotating and
    // dragging a polyline behave like every other wall kind.
    glm::vec2 centroid{0.0f, 0.0f};
    for (const glm::vec2& point : draft.points) {
        centroid += point;
    }
    centroid /= static_cast<float>(draft.points.size());

    logic::PolylineShape line;
    line.thickness = thickness;
    line.points.reserve(draft.points.size());
    for (const glm::vec2& point : draft.points) {
        line.points.push_back(point - centroid);
    }

    out = logic::Wall{};
    out.center = centroid;
    out.rotation = 0.0f;
    out.color = color;
    out.shape = std::move(line);
    return true;
}
```

- [ ] **Step 3: Add the draft to `EditorScene`**

Add the member `editor::PolylineDraft m_draft;` to `EditorScene.hpp`.

The thickness of a new polyline belongs on `EditorState`, not the scene, because the toolbar in `EditorUi.cpp` has to edit it. Add to `EditorState`, beside `newWallColor`:

```cpp
    // Thickness applied to the next polyline drawn.
    float newPolylineThickness = 6.0f;
```

- [ ] **Step 4: Handle polyline input in `EditorScene::handleEvent`**

Add before the box-tool block:

```cpp
    if (!io.WantCaptureMouse && m_state.tool == editor::Tool::Polyline) {
        switch (event.type) {
            case SDL_EVENT_MOUSE_MOTION:
                m_draft.cursor = m_cursorWorld;
                return m_draft.active;

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button != SDL_BUTTON_LEFT) {
                    break;
                }
                // A double click finishes rather than adding a duplicate point.
                if (event.button.clicks >= 2) {
                    commitDraft();
                    return true;
                }
                m_draft.active = true;
                m_draft.points.push_back(m_cursorWorld);
                return true;
            }

            default:
                break;
        }
    }
```

And in the key switch, before the existing `SDLK_ESCAPE` case, replace that case with:

```cpp
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                if (m_draft.active) {
                    commitDraft();
                    return true;
                }
                break;

            case SDLK_ESCAPE:
                // Escape abandons an in-progress polyline entirely, rather than
                // committing a partial one.
                m_draft.clear();
                m_placement.active = false;
                m_state.tool = editor::Tool::Select;
                return true;
```

- [ ] **Step 5: Add `commitDraft` to `EditorScene`**

Declare `void commitDraft();` in the header, and define:

```cpp
void EditorScene::commitDraft() {
    logic::Wall wall;
    if (editor::finishPolyline(m_draft, m_state.newWallColor, m_state.newPolylineThickness, wall)) {
        m_state.beginMutation();
        m_state.level.walls.push_back(std::move(wall));
    }
    m_draft.clear();
    // Tool stays armed, ready for the next polyline.
}
```

- [ ] **Step 6: Draw the draft preview**

In `EditorScene::render`, after the placement preview:

```cpp
    // The in-progress polyline, plus a rubber-band segment to the cursor so you
    // can see where the next click would land.
    if (m_draft.active && !m_draft.points.empty()) {
        editor::PolylineDraft preview = m_draft;
        preview.points.push_back(m_draft.cursor);

        logic::Wall wall;
        if (editor::finishPolyline(preview, m_state.newWallColor, m_state.newPolylineThickness, wall)) {
            glm::vec4 tint = gfx::toFloatColor(wall.color);
            tint.a = 0.55f;
            gfx::renderWall(wall, batch, atlas, tint);
        }
    }
```

- [ ] **Step 7: Add a thickness control to the toolbar**

In `drawToolbar`, after the colour picker:

```cpp
    ImGui::SeparatorText("New polyline");
    ImGui::DragFloat("Thickness", &state.newPolylineThickness, 0.25f, 1.0f, 200.0f);
```

- [ ] **Step 8: Build and verify**

Expected: press 5 for Polyline. Each click drops a point; a rubber-band segment follows the cursor from the last point. Enter or a double click finishes the line and leaves the tool armed for the next. Escape mid-draw discards the whole in-progress line and returns to Select. A finished polyline is **not closed** — a two-click line is a single straight wall, which is the flat-wall case. Switch to Select and drag the polyline: it moves as a whole. Set Rotation in the inspector and the whole line rotates about its centroid. Ctrl+Z removes the whole polyline in one step.

- [ ] **Step 9: Commit**

```bash
git add src/editor/Tools.hpp src/editor/Tools.cpp src/editor/EditorState.hpp src/editor/EditorUi.cpp src/scene/EditorScene.hpp src/scene/EditorScene.cpp
git commit -m "Add the polyline tool

Click to drop points, Enter or double click to finish, Escape to discard
the whole in-progress line. Lines are open: a two-point polyline is a
flat wall, which is the common case rather than a degenerate one.

Points are stored relative to their centroid so that moving and rotating
a polyline behaves exactly as it does for every other wall kind."
```

---

## Task 10: Resize and rotate handles

**Files:**
- Create: `src/editor/Handles.hpp`, `src/editor/Handles.cpp`
- Modify: `src/scene/EditorScene.hpp`, `src/scene/EditorScene.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `logic::localWallBounds`, `logic::worldToLocal`, `logic::localToWorld` (Task 2).
- Produces: `editor::HandleKind`, `editor::Handle`, `editor::wallHandles(const logic::Wall&) -> std::vector<Handle>`, `editor::pickHandle(const logic::Wall&, glm::vec2, float) -> Handle`, `editor::applyHandleDrag(logic::Wall&, const Handle&, glm::vec2)`, `editor::kRotateHandleGap`.

- [ ] **Step 1: Create `src/editor/Handles.hpp`**

```cpp
#pragma once

#include <glm/vec2.hpp>

#include <vector>

#include "logic/Level.hpp"

namespace horde::editor {

// How far beyond a wall's local bounds the rotate handle floats, in world units.
inline constexpr float kRotateHandleGap = 22.0f;

enum class HandleKind { None, ResizeCorner, Rotate };

// One draggable grip on a selected wall.
//
// `corner` is 0..3 for ResizeCorner, ordered (min,min), (max,min), (min,max),
// (max,max) in the wall's LOCAL space, and unused otherwise.
struct Handle {
    HandleKind kind = HandleKind::None;
    int corner = 0;
    glm::vec2 world{0.0f, 0.0f};
};

// The handles this wall has, in world space.
//
// A wall kind reports its own handles rather than every caller assuming four
// corners. That is what makes per-vertex polyline handles a later addition to
// one function rather than a rewrite of the drag code.
std::vector<Handle> wallHandles(const logic::Wall& wall);

// The handle within `pickRadius` world units of `world`, or one with kind None.
// Rotate wins ties, because it sits outside the shape where nothing else is.
Handle pickHandle(const logic::Wall& wall, glm::vec2 world, float pickRadius);

// Applies a drag of `handle` to `world`, mutating the wall.
//
// `rotationGrabOffset` is the angle between the wall's rotation and the cursor
// at the moment the rotate handle was grabbed; passing it keeps the shape from
// snapping round to meet the cursor on the first frame. It is ignored for
// resize handles.
void applyHandleDrag(logic::Wall& wall, const Handle& handle, glm::vec2 world, float rotationGrabOffset);

} // namespace horde::editor
```

- [ ] **Step 2: Create `src/editor/Handles.cpp`**

```cpp
#include "editor/Handles.hpp"

#include <algorithm>
#include <cmath>

#include "logic/LevelGeometry.hpp"

namespace horde::editor {

std::vector<Handle> wallHandles(const logic::Wall& wall) {
    const logic::Aabb local = logic::localWallBounds(wall);

    std::vector<Handle> handles;
    handles.reserve(5);

    const glm::vec2 corners[4] = {
        {local.min.x, local.min.y},
        {local.max.x, local.min.y},
        {local.min.x, local.max.y},
        {local.max.x, local.max.y},
    };

    for (int i = 0; i < 4; ++i) {
        handles.push_back(Handle{HandleKind::ResizeCorner, i, logic::localToWorld(wall, corners[i])});
    }

    // Circles ignore rotation, so they get no rotate handle. Everything else
    // gets one floating above the top edge, clear of the corners.
    if (!std::holds_alternative<logic::CircleShape>(wall.shape)) {
        const glm::vec2 above{(local.min.x + local.max.x) * 0.5f, local.min.y - kRotateHandleGap};
        handles.push_back(Handle{HandleKind::Rotate, 0, logic::localToWorld(wall, above)});
    }

    return handles;
}

Handle pickHandle(const logic::Wall& wall, glm::vec2 world, float pickRadius) {
    Handle best;
    float bestDistance = pickRadius;

    for (const Handle& handle : wallHandles(wall)) {
        const glm::vec2 d = world - handle.world;
        const float distance = std::sqrt(d.x * d.x + d.y * d.y);

        // Strictly-less keeps the first best; rotate is pushed last but sits
        // clear of the corners, so ties in practice do not arise.
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = handle;
        }
    }

    return best;
}

void applyHandleDrag(logic::Wall& wall, const Handle& handle, glm::vec2 world, float rotationGrabOffset) {
    if (handle.kind == HandleKind::Rotate) {
        const glm::vec2 d = world - wall.center;
        wall.rotation = std::atan2(d.y, d.x) - rotationGrabOffset;
        return;
    }

    if (handle.kind != HandleKind::ResizeCorner) {
        return;
    }

    // Resizing happens in local space, where the shape is axis aligned: the
    // dragged corner's distance from the centre IS the new half-extent. The
    // wall stays centred, so the opposite corner moves too — which is the
    // behaviour the inspector's Width/Height fields also produce.
    const glm::vec2 local = logic::worldToLocal(wall, world);
    const glm::vec2 half{std::max(std::abs(local.x), 1.0f), std::max(std::abs(local.y), 1.0f)};

    if (auto* rect = std::get_if<logic::RectangleShape>(&wall.shape)) {
        rect->halfExtents = half;
        return;
    }

    if (auto* tri = std::get_if<logic::TriangleShape>(&wall.shape)) {
        tri->halfExtents = half;
        return;
    }

    if (auto* circle = std::get_if<logic::CircleShape>(&wall.shape)) {
        circle->radius = std::max(half.x, half.y);
        return;
    }

    // A polyline has no extents of its own, so a corner drag scales its points
    // about the centre by the ratio the corner moved.
    auto& line = std::get<logic::PolylineShape>(wall.shape);
    const logic::Aabb bounds = logic::localWallBounds(wall);
    const glm::vec2 current{std::max(std::abs(bounds.min.x), std::abs(bounds.max.x)),
                            std::max(std::abs(bounds.min.y), std::abs(bounds.max.y))};

    if (current.x <= 0.0f || current.y <= 0.0f) {
        return;
    }

    const glm::vec2 scale = half / current;
    for (glm::vec2& point : line.points) {
        point *= scale;
    }
}

} // namespace horde::editor
```

- [ ] **Step 3: Add handle drag state to `EditorScene.hpp`**

```cpp
    editor::Handle m_activeHandle;
    float m_rotationGrabOffset = 0.0f;
```

Add `#include "editor/Handles.hpp"`.

- [ ] **Step 4: Give handles priority in the Select-tool mouse-down**

Replace the `SDL_EVENT_MOUSE_BUTTON_DOWN` case in the Select block with:

```cpp
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button != SDL_BUTTON_LEFT) {
                    break;
                }

                // Handles beat the body: a corner handle sits ON the shape's
                // outline, so testing the body first would make it unreachable.
                if (m_state.selection.isWall()) {
                    const logic::Wall& wall = m_state.level.walls[m_state.selection.index];
                    const editor::Handle handle = editor::pickHandle(wall, m_cursorWorld, handlePickRadius());
                    if (handle.kind != editor::HandleKind::None) {
                        m_state.beginMutation();
                        m_activeHandle = handle;
                        if (handle.kind == editor::HandleKind::Rotate) {
                            const glm::vec2 d = m_cursorWorld - wall.center;
                            m_rotationGrabOffset = std::atan2(d.y, d.x) - wall.rotation;
                        }
                        return true;
                    }
                }

                m_state.selection = editor::pick(m_state.level, m_cursorWorld);
                if (m_state.selection.isWall()) {
                    m_state.beginMutation();
                    m_draggingBody = true;
                    m_dragGrabOffset = m_cursorWorld - m_state.level.walls[m_state.selection.index].center;
                }
                return true;
            }
```

In the motion case, handle dragging comes first:

```cpp
            case SDL_EVENT_MOUSE_MOTION: {
                if (m_activeHandle.kind != editor::HandleKind::None && m_state.selection.isWall()) {
                    editor::applyHandleDrag(m_state.level.walls[m_state.selection.index], m_activeHandle,
                                            m_cursorWorld, m_rotationGrabOffset);
                    return true;
                }
                if (m_draggingBody && m_state.selection.isWall()) {
                    m_state.level.walls[m_state.selection.index].center = m_cursorWorld - m_dragGrabOffset;
                    return true;
                }
                m_state.hovered = editor::pick(m_state.level, m_cursorWorld);
                return false;
            }
```

And in the button-up case, add `m_activeHandle = editor::Handle{};` next to `m_draggingBody = false;`.

- [ ] **Step 5: Size handles in world units from the zoom**

Handles must stay a constant size on screen no matter the zoom, or they become unclickable when zoomed out. Add to `EditorScene`:

```cpp
// Handles are drawn and picked at a constant SCREEN size, so convert once per
// use: at zoom z, one screen pixel is 1/z world units.
float EditorScene::handleWorldSize() const {
    return 10.0f / m_camera.zoom();
}

float EditorScene::handlePickRadius() const {
    return handleWorldSize();
}
```

Declare both in the header.

- [ ] **Step 6: Draw the handles**

At the end of `EditorScene::render`:

```cpp
    if (m_state.selection.isWall() && m_state.selection.index < m_state.level.walls.size()) {
        const float size = handleWorldSize();
        for (const editor::Handle& handle : editor::wallHandles(m_state.level.walls[m_state.selection.index])) {
            const glm::vec4 uv =
                handle.kind == editor::HandleKind::Rotate ? gfx::cellDisc() : gfx::cellSolid();
            const glm::vec4 color = handle.kind == editor::HandleKind::Rotate
                                        ? glm::vec4{0.4f, 0.8f, 1.0f, 1.0f}
                                        : glm::vec4{1.0f, 0.9f, 0.3f, 1.0f};
            gfx::drawCentered(batch, atlas, handle.world, {size, size}, 0.0f, uv, color);
        }
    }
```

- [ ] **Step 7: Register, build, verify**

Add `src/editor/Handles.cpp` to `CMakeLists.txt`, then build and run.

Expected: selecting a wall shows four yellow square handles at the corners of its bounding box and one blue round handle floating above it. Dragging a corner resizes about the centre. Dragging the blue handle rotates, and **the shape does not jump on the first frame** — that is what `rotationGrabOffset` buys. Rotate a rectangle 45 degrees, then drag a corner: it resizes along the wall's own axes, not the screen's, which is the check that local-space resizing works. A circle has **four corner handles and no rotate handle**. Zoom right out: handles stay the same size on screen and remain clickable.

- [ ] **Step 8: Commit**

```bash
git add src/editor/Handles.hpp src/editor/Handles.cpp src/scene/EditorScene.hpp src/scene/EditorScene.cpp CMakeLists.txt
git commit -m "Add resize and rotate handles

Each wall kind reports its own handles, so adding per-vertex polyline
handles later touches one function rather than the drag code. Resizing
works in the wall's local space, so a rotated wall resizes along its own
axes rather than the screen's.

Handles are sized in world units derived from the camera zoom so they
stay a constant size on screen and remain clickable when zoomed out."
```

---

## Task 11: Grid and rotation snapping

**Files:**
- Create: `src/editor/Snap.hpp`
- Modify: `src/editor/EditorUi.cpp` (snap panel)
- Modify: `src/scene/EditorScene.cpp` (apply at each call site)

**Interfaces:**
- Consumes: `SnapSettings` (Task 5).
- Produces: `editor::snapPosition(glm::vec2, const SnapSettings&) -> glm::vec2`, `editor::snapRotation(float radians, const SnapSettings&) -> float`.

- [ ] **Step 1: Create `src/editor/Snap.hpp`**

```cpp
#pragma once

#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>

#include <cmath>

#include "editor/EditorState.hpp"

namespace horde::editor {

// Rounds a world position to the grid, if position snapping is on.
//
// The grid is anchored at the level origin, and it is a wall's CENTRE that
// snaps — the one point every wall kind has, which keeps this one rule rather
// than four.
inline glm::vec2 snapPosition(glm::vec2 world, const SnapSettings& snap) {
    if (!snap.position || snap.gridSize <= 0.0f) {
        return world;
    }
    return {std::round(world.x / snap.gridSize) * snap.gridSize,
            std::round(world.y / snap.gridSize) * snap.gridSize};
}

// Rounds an angle in radians to the nearest snap increment, if rotation
// snapping is on.
inline float snapRotation(float radians, const SnapSettings& snap) {
    if (!snap.rotation || snap.rotationDegrees <= 0.0f) {
        return radians;
    }
    const float step = glm::radians(snap.rotationDegrees);
    return std::round(radians / step) * step;
}

} // namespace horde::editor
```

- [ ] **Step 2: Add the snapping panel to `drawLevelPanel` in `EditorUi.cpp`**

Before the History section:

```cpp
    ImGui::SeparatorText("Snapping");

    ImGui::Checkbox("Snap position", &state.snap.position);
    ImGui::BeginDisabled(!state.snap.position);
    ImGui::DragFloat("Grid", &state.snap.gridSize, 0.5f, 1.0f, 500.0f, "%.1f");
    ImGui::EndDisabled();

    ImGui::Checkbox("Snap rotation", &state.snap.rotation);
    ImGui::BeginDisabled(!state.snap.rotation);
    ImGui::DragFloat("Step", &state.snap.rotationDegrees, 0.5f, 1.0f, 180.0f, "%.1f deg");
    ImGui::EndDisabled();
```

- [ ] **Step 3: Apply snapping at every call site in `EditorScene.cpp`**

Add `#include "editor/Snap.hpp"`. Then, four changes:

Body drag, in the motion case:
```cpp
                if (m_draggingBody && m_state.selection.isWall()) {
                    m_state.level.walls[m_state.selection.index].center =
                        editor::snapPosition(m_cursorWorld - m_dragGrabOffset, m_state.snap);
                    return true;
                }
```

Handle drag, in the motion case — snap the cursor before it reaches the handle, and re-snap the resulting rotation:
```cpp
                if (m_activeHandle.kind != editor::HandleKind::None && m_state.selection.isWall()) {
                    logic::Wall& wall = m_state.level.walls[m_state.selection.index];
                    const glm::vec2 target = m_activeHandle.kind == editor::HandleKind::ResizeCorner
                                                 ? editor::snapPosition(m_cursorWorld, m_state.snap)
                                                 : m_cursorWorld;
                    editor::applyHandleDrag(wall, m_activeHandle, target, m_rotationGrabOffset);
                    if (m_activeHandle.kind == editor::HandleKind::Rotate) {
                        wall.rotation = editor::snapRotation(wall.rotation, m_state.snap);
                    }
                    return true;
                }
```

Box placement, in the button-down and button-up cases — snap both ends of the drag:
```cpp
                    m_placement.start = editor::snapPosition(m_cursorWorld, m_state.snap);
```
and in button-up, pass `editor::snapPosition(m_cursorWorld, m_state.snap)` as the drag end to `makeWallFromDrag`, and use the same snapped value for `m_placement.current` in the motion case so the preview matches what will be produced.

Polyline points, in the polyline button-down case:
```cpp
                m_draft.points.push_back(editor::snapPosition(m_cursorWorld, m_state.snap));
```
and set `m_draft.cursor = editor::snapPosition(m_cursorWorld, m_state.snap);` in its motion case.

- [ ] **Step 4: Build and verify**

Expected, with both toggles **off** by default: placement and rotation are completely free, at any angle and any position. Turn Snap position on with a grid of 10: a dragged wall's centre lands only on multiples of 10, and swept boxes have edges on the grid. Turn Snap rotation on with a step of 15: the rotate handle jumps between 15-degree increments while position stays free — the check that the two toggles are genuinely independent. Turn both off again and confirm free placement returns.

- [ ] **Step 5: Commit**

```bash
git add src/editor/Snap.hpp src/editor/EditorUi.cpp src/scene/EditorScene.cpp
git commit -m "Add independent grid and rotation snapping

Two toggles rather than one, because wanting axis-aligned angles while
placing freely (or the reverse) is routine. Both default to off:
off-grid, off-axis placement is the norm and snapping is the opt-in aid.

Position snapping constrains a wall's centre, the one point every wall
kind has, so there is one rule rather than four."
```

---

## Task 12: Placing and stretching markers

**Files:**
- Create: `src/editor/Markers.hpp`, `src/editor/Markers.cpp`
- Modify: `src/scene/EditorScene.hpp`, `src/scene/EditorScene.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `logic::Marker`, `logic::edgeLength`, `logic::markerRect` (Task 2).
- Produces: `editor::kMinimumMarkerLength`, `editor::nearestEdge`, `editor::projectOntoEdge`, `editor::freeSpan`, `editor::clampMarker`, `editor::makeMarkerFromDrag`, `editor::markerEndHandles`.

**Design note:** markers never overlap because a drag is **clamped against its neighbours**, not because overlap is validated afterwards. That is why `logic::validate` has no marker rules. See `docs/adr/0002-edge-relative-markers.md`.

- [ ] **Step 1: Create `src/editor/Markers.hpp`**

```cpp
#pragma once

#include <cstddef>

#include <glm/vec2.hpp>

#include "logic/Level.hpp"

namespace horde::editor {

// Shorter than this and a marker is a mis-click rather than a placement.
inline constexpr float kMinimumMarkerLength = 16.0f;

// Which edge of the level a world point is closest to.
logic::Edge nearestEdge(const logic::Level& level, glm::vec2 world);

// Distance along `edge` from its start corner, clamped to the edge. North and
// south run along x; east and west run along y.
float projectOntoEdge(const logic::Level& level, logic::Edge edge, glm::vec2 world);

// The largest free interval on `edge` that contains `at`, ignoring the marker at
// `ignoreIndex` (pass level.markers.size() when placing a new one).
//
// This is what makes overlap impossible: a drag is clamped into this span, so
// two markers can never occupy the same stretch of an edge even transiently.
void freeSpan(const logic::Level& level, logic::Edge edge, float at, std::size_t ignoreIndex, float& outLow,
              float& outHigh);

// Clamps the marker at `index` into its free span, shortening it if needed.
// Call after anything that could invalidate it, including a level resize.
void clampMarker(logic::Level& level, std::size_t index);

// Clamps every marker. Call after the level's size changes.
void clampAllMarkers(logic::Level& level);

// Builds a marker from a drag along the edge nearest `start`. Returns false if
// the resulting band would be shorter than kMinimumMarkerLength or if there is
// no free room where the drag began.
bool makeMarkerFromDrag(const logic::Level& level, logic::MarkerKind kind, glm::vec2 start, glm::vec2 end,
                        logic::Marker& out);

// The world positions of a marker's two end grips: the low-offset end first.
void markerEndHandles(const logic::Level& level, const logic::Marker& marker, glm::vec2& outLow, glm::vec2& outHigh);

} // namespace horde::editor
```

- [ ] **Step 2: Create `src/editor/Markers.cpp`**

```cpp
#include "editor/Markers.hpp"

#include <algorithm>
#include <cmath>

namespace horde::editor {
namespace {

bool runsAlongX(logic::Edge edge) {
    return edge == logic::Edge::North || edge == logic::Edge::South;
}

} // namespace

logic::Edge nearestEdge(const logic::Level& level, glm::vec2 world) {
    const float toNorth = std::abs(world.y);
    const float toSouth = std::abs(level.size.y - world.y);
    const float toWest = std::abs(world.x);
    const float toEast = std::abs(level.size.x - world.x);

    float best = toNorth;
    logic::Edge edge = logic::Edge::North;

    if (toSouth < best) { best = toSouth; edge = logic::Edge::South; }
    if (toWest  < best) { best = toWest;  edge = logic::Edge::West;  }
    if (toEast  < best) { best = toEast;  edge = logic::Edge::East;  }

    return edge;
}

float projectOntoEdge(const logic::Level& level, logic::Edge edge, glm::vec2 world) {
    const float along = runsAlongX(edge) ? world.x : world.y;
    return std::clamp(along, 0.0f, logic::edgeLength(level, edge));
}

void freeSpan(const logic::Level& level, logic::Edge edge, float at, std::size_t ignoreIndex, float& outLow,
              float& outHigh) {
    outLow = 0.0f;
    outHigh = logic::edgeLength(level, edge);

    for (std::size_t i = 0; i < level.markers.size(); ++i) {
        if (i == ignoreIndex) {
            continue;
        }
        const logic::Marker& other = level.markers[i];
        if (other.edge != edge) {
            continue; // each edge is its own interval space; corners never clash
        }

        const float start = other.offset;
        const float end = other.offset + other.length;

        if (end <= at) {
            outLow = std::max(outLow, end);
        } else if (start >= at) {
            outHigh = std::min(outHigh, start);
        } else {
            // `at` is inside another marker: there is no free room here.
            outLow = at;
            outHigh = at;
            return;
        }
    }
}

void clampMarker(logic::Level& level, std::size_t index) {
    if (index >= level.markers.size()) {
        return;
    }

    logic::Marker& marker = level.markers[index];
    const float edge = logic::edgeLength(level, marker.edge);

    // Anchor the clamp on the marker's own midpoint so it keeps its place
    // rather than jumping to an unrelated gap.
    const float midpoint = std::clamp(marker.offset + marker.length * 0.5f, 0.0f, edge);

    float low = 0.0f;
    float high = edge;
    freeSpan(level, marker.edge, midpoint, index, low, high);

    const float available = high - low;
    marker.length = std::clamp(marker.length, std::min(kMinimumMarkerLength, available), std::max(available, 0.0f));
    marker.offset = std::clamp(marker.offset, low, std::max(low, high - marker.length));
}

void clampAllMarkers(logic::Level& level) {
    for (std::size_t i = 0; i < level.markers.size(); ++i) {
        clampMarker(level, i);
    }
}

bool makeMarkerFromDrag(const logic::Level& level, logic::MarkerKind kind, glm::vec2 start, glm::vec2 end,
                        logic::Marker& out) {
    const logic::Edge edge = nearestEdge(level, start);
    const float a = projectOntoEdge(level, edge, start);
    const float b = projectOntoEdge(level, edge, end);

    float low = 0.0f;
    float high = logic::edgeLength(level, edge);
    freeSpan(level, edge, a, level.markers.size(), low, high);

    const float from = std::clamp(std::min(a, b), low, high);
    const float to = std::clamp(std::max(a, b), low, high);

    if (to - from < kMinimumMarkerLength) {
        return false;
    }

    out = logic::Marker{};
    out.kind = kind;
    out.edge = edge;
    out.offset = from;
    out.length = to - from;
    return true;
}

void markerEndHandles(const logic::Level& level, const logic::Marker& marker, glm::vec2& outLow, glm::vec2& outHigh) {
    glm::vec2 center{};
    glm::vec2 size{};
    logic::markerRect(level, marker, center, size);

    if (runsAlongX(marker.edge)) {
        outLow = {marker.offset, center.y};
        outHigh = {marker.offset + marker.length, center.y};
    } else {
        outLow = {center.x, marker.offset};
        outHigh = {center.x, marker.offset + marker.length};
    }
}

} // namespace horde::editor
```

- [ ] **Step 3: Add marker interaction state to `EditorScene.hpp`**

```cpp
    // 0 = not dragging an end, 1 = the low end, 2 = the high end, 3 = the body.
    int m_markerDrag = 0;
    float m_markerGrabOffset = 0.0f;
```

Add `#include "editor/Markers.hpp"`.

- [ ] **Step 4: Place markers with the Spawn and Exit tools**

Add this block in `handleEvent`, beside the box-tool block. It reuses `m_placement` because the gesture is still a drag:

```cpp
    if (!io.WantCaptureMouse && (m_state.tool == editor::Tool::Spawn || m_state.tool == editor::Tool::Exit)) {
        const logic::MarkerKind kind =
            m_state.tool == editor::Tool::Spawn ? logic::MarkerKind::Spawn : logic::MarkerKind::Exit;

        switch (event.type) {
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    m_placement.active = true;
                    m_placement.start = m_cursorWorld;
                    m_placement.current = m_cursorWorld;
                    return true;
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                m_placement.current = m_cursorWorld;
                return m_placement.active;

            case SDL_EVENT_MOUSE_BUTTON_UP: {
                if (event.button.button != SDL_BUTTON_LEFT || !m_placement.active) {
                    break;
                }
                m_placement.active = false;

                logic::Marker marker;
                if (editor::makeMarkerFromDrag(m_state.level, kind, m_placement.start, m_cursorWorld, marker)) {
                    m_state.beginMutation();
                    m_state.level.markers.push_back(marker);
                }
                return true;
            }

            default:
                break;
        }
    }
```

- [ ] **Step 5: Drag markers in Select mode**

In the Select block's mouse-down, after the wall-handle check and before `editor::pick`, add:

```cpp
                if (m_state.selection.isMarker()) {
                    const logic::Marker& marker = m_state.level.markers[m_state.selection.index];
                    glm::vec2 low{};
                    glm::vec2 high{};
                    editor::markerEndHandles(m_state.level, marker, low, high);

                    const float reach = handlePickRadius();
                    const auto near = [&](glm::vec2 p) {
                        const glm::vec2 d = m_cursorWorld - p;
                        return std::sqrt(d.x * d.x + d.y * d.y) <= reach;
                    };

                    if (near(low) || near(high)) {
                        m_state.beginMutation();
                        m_markerDrag = near(low) ? 1 : 2;
                        return true;
                    }
                }
```

Then, after `m_state.selection = editor::pick(...)`, add the body-drag case:

```cpp
                if (m_state.selection.isMarker()) {
                    const logic::Marker& marker = m_state.level.markers[m_state.selection.index];
                    m_state.beginMutation();
                    m_markerDrag = 3;
                    m_markerGrabOffset =
                        editor::projectOntoEdge(m_state.level, marker.edge, m_cursorWorld) - marker.offset;
                    return true;
                }
```

In the motion case, before the wall-body branch:

```cpp
                if (m_markerDrag != 0 && m_state.selection.isMarker()) {
                    logic::Marker& marker = m_state.level.markers[m_state.selection.index];
                    const float along = editor::projectOntoEdge(m_state.level, marker.edge, m_cursorWorld);

                    if (m_markerDrag == 1) {
                        const float end = marker.offset + marker.length;
                        marker.offset = std::min(along, end - editor::kMinimumMarkerLength);
                        marker.length = end - marker.offset;
                    } else if (m_markerDrag == 2) {
                        marker.length = std::max(along - marker.offset, editor::kMinimumMarkerLength);
                    } else {
                        marker.offset = along - m_markerGrabOffset;
                    }

                    // Clamping here is what makes overlap impossible, rather
                    // than something validation reports after the fact.
                    editor::clampMarker(m_state.level, m_state.selection.index);
                    return true;
                }
```

In the button-up case add `m_markerDrag = 0;`.

- [ ] **Step 6: Re-clamp markers when the level is resized**

In `EditorUi.cpp`'s `drawLevelPanel`, add `#include "editor/Markers.hpp"` and change the size handler:

```cpp
    if (ImGui::DragFloat2("Size", size, 1.0f, 50.0f, 10000.0f, "%.0f")) {
        state.beginMutation();
        state.level.size = {size[0], size[1]};
        clampAllMarkers(state.level);
    }
```

- [ ] **Step 7: Draw marker end handles**

In `EditorScene::render`, alongside the wall handle block:

```cpp
    if (m_state.selection.isMarker() && m_state.selection.index < m_state.level.markers.size()) {
        const float size = handleWorldSize();
        glm::vec2 low{};
        glm::vec2 high{};
        editor::markerEndHandles(m_state.level, m_state.level.markers[m_state.selection.index], low, high);

        for (const glm::vec2& position : {low, high}) {
            gfx::drawCentered(batch, atlas, position, {size, size}, 0.0f, gfx::cellSolid(),
                              {1.0f, 1.0f, 1.0f, 1.0f});
        }
    }
```

- [ ] **Step 8: Register, build, verify**

Add `src/editor/Markers.cpp` to `CMakeLists.txt`. Then, in the editor:

- Press 6 (Spawn), drag along the **top** edge — a green band appears there, flat against the edge. Press 7 (Exit), drag along the **bottom** — a red band appears. Dragging in the middle of the level still snaps the marker to whichever edge is nearest; there is no way to place one in the interior.
- Select a marker: two white end grips appear. Drag one to stretch the band **along** the edge; there is no gesture that makes it thicker, which is the whole point.
- Drag a new marker so it would overlap an existing one on the same edge: it stops at the neighbour rather than passing through or being rejected.
- Drag a marker's body toward a neighbour: it stops flush against it.
- Shrink the level's Size so an edge becomes shorter than its markers: every marker stays on its edge, shortened as needed, never overlapping.
- Select the only spawn and press Delete: refused with the status message. Add a second spawn, then delete either: allowed.

- [ ] **Step 9: Commit**

```bash
git add src/editor/Markers.hpp src/editor/Markers.cpp src/editor/EditorUi.cpp src/scene/EditorScene.hpp src/scene/EditorScene.cpp CMakeLists.txt
git commit -m "Add marker placement and edge-constrained stretching

Markers are placed by dragging along whichever level edge is nearest, and
stretched by their two end grips. Every drag is clamped into the free
span between neighbours on the same edge, so overlapping markers cannot
exist even transiently and no validation rule needs to look for them.

Resizing a level re-clamps every marker onto its shortened edge."
```

---

## Task 13: Saving, loading, and the validation panel

**Files:**
- Create: `src/logic/LevelFiles.hpp`, `src/logic/LevelFiles.cpp`
- Modify: `src/editor/EditorUi.cpp` (files panel, validation panel)
- Modify: `CMakeLists.txt`

`LevelFiles` lives in `logic/`, not `editor/`, because Task 14's main menu needs to enumerate levels too.

**Interfaces:**
- Consumes: `logic::loadLevel`, `logic::saveLevel` (Task 3), `logic::validate` (Task 2).
- Produces: `logic::levelsDirectory()`, `logic::listLevels()`, `logic::levelDisplayName(const std::filesystem::path&)`, `logic::levelPathForName(const std::string&)`.

- [ ] **Step 1: Create `src/logic/LevelFiles.hpp`**

```cpp
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace horde::logic {

// The suffix identifying a level file. Doubled extension so editors syntax
// highlight it as JSON while it remains recognisably a level.
inline constexpr const char* kLevelExtension = ".level.json";

// base()/assets/levels — the one directory levels live in. It ships next to the
// executable via the horde_runtime_files build step.
std::filesystem::path levelsDirectory();

// Every level file in levelsDirectory(), sorted by filename. Empty if the
// directory does not exist; never throws.
std::vector<std::filesystem::path> listLevels();

// "assets/levels/arena.level.json" -> "arena". For display and for pre-filling
// the save box.
std::string levelDisplayName(const std::filesystem::path& path);

// "arena" -> levelsDirectory()/"arena.level.json". Strips path separators and
// anything else that would let a name escape the levels directory.
std::filesystem::path levelPathForName(const std::string& name);

} // namespace horde::logic
```

- [ ] **Step 2: Create `src/logic/LevelFiles.cpp`**

```cpp
#include "logic/LevelFiles.hpp"

#include <algorithm>
#include <cctype>

#include "core/Paths.hpp"

namespace horde::logic {

std::filesystem::path levelsDirectory() {
    return paths::asset("levels");
}

std::vector<std::filesystem::path> listLevels() {
    std::vector<std::filesystem::path> found;

    std::error_code code;
    const std::filesystem::path directory = levelsDirectory();
    if (!std::filesystem::is_directory(directory, code)) {
        return found;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, code)) {
        if (!entry.is_regular_file(code)) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        const std::string suffix = kLevelExtension;
        if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            found.push_back(entry.path());
        }
    }

    std::sort(found.begin(), found.end());
    return found;
}

std::string levelDisplayName(const std::filesystem::path& path) {
    std::string name = path.filename().string();
    const std::string suffix = kLevelExtension;
    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
        name.erase(name.size() - suffix.size());
    }
    return name;
}

std::filesystem::path levelPathForName(const std::string& name) {
    // Keep only characters that cannot form a path, so a name typed into the
    // save box can never write outside the levels directory.
    std::string safe;
    safe.reserve(name.size());
    for (const char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) != 0 || c == '-' || c == '_' || c == ' ') {
            safe.push_back(c);
        }
    }

    if (safe.empty()) {
        safe = "untitled";
    }

    return levelsDirectory() / (safe + kLevelExtension);
}

} // namespace horde::logic
```

- [ ] **Step 3: Add the files panel to `EditorUi.cpp`**

Add includes `"logic/LevelFiles.hpp"`, `"logic/LevelIO.hpp"`, `"logic/LevelValidation.hpp"`, `<cstring>`. Then, in the anonymous namespace:

```cpp
void drawFilesPanel(EditorState& state) {
    ImGui::SetNextWindowPos(ImVec2(600.0f, 20.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280.0f, 300.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Files");

    // A plain list of the levels that actually exist beats a native dialog that
    // opens somewhere unrelated, and works identically on all three platforms.
    ImGui::SeparatorText("Open");

    const std::vector<std::filesystem::path> levels = logic::listLevels();
    if (levels.empty()) {
        ImGui::TextDisabled("No levels saved yet.");
    }

    for (const std::filesystem::path& path : levels) {
        const std::string name = logic::levelDisplayName(path);
        if (ImGui::Selectable(name.c_str(), state.path == path)) {
            std::string error;
            if (std::optional<logic::Level> loaded = logic::loadLevel(path, &error)) {
                state.level = std::move(*loaded);
                state.path = path;
                state.selection.clear();
                state.hovered.clear();
                state.undo.clear();
                state.dirty = false;
                state.status = "Opened " + name;
            } else {
                state.status = "Could not open " + name + ": " + error;
            }
        }
    }

    ImGui::SeparatorText("Save");

    static char nameBuffer[128] = "untitled";
    ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer));

    const std::vector<logic::Problem> problems = logic::validate(state.level);

    ImGui::BeginDisabled(!problems.empty());
    if (ImGui::Button("Save", ImVec2(120.0f, 0.0f))) {
        const std::filesystem::path path = logic::levelPathForName(nameBuffer);
        std::string error;
        if (logic::saveLevel(state.level, path, &error)) {
            state.path = path;
            state.dirty = false;
            state.status = "Saved " + logic::levelDisplayName(path);
        } else {
            state.status = "Could not save: " + error;
        }
    }
    ImGui::EndDisabled();

    if (!problems.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "fix %zu problem(s)", problems.size());
    }

    ImGui::SameLine();
    if (ImGui::Button("New", ImVec2(80.0f, 0.0f))) {
        state.level = logic::makeDefaultLevel();
        state.path.clear();
        state.selection.clear();
        state.hovered.clear();
        state.undo.clear();
        state.dirty = false;
        state.status = "New level";
    }

    if (state.dirty) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Unsaved changes");
    }

    ImGui::SeparatorText("Problems");

    if (problems.empty()) {
        ImGui::TextDisabled("None. This level can be saved.");
    }

    for (const logic::Problem& problem : problems) {
        char label[160];
        std::snprintf(label, sizeof(label), "Wall %zu: %s##problem%zu", problem.wallIndex, problem.message.c_str(),
                      problem.wallIndex);
        if (ImGui::Selectable(label)) {
            state.selection = Selection{SelectionKind::Wall, problem.wallIndex};
        }
    }

    ImGui::End();
}
```

Call it from `drawEditorUi`.

Note the `New` and `Open` paths clear the undo stack: history from a different level is not history of this one, and letting Ctrl+Z resurrect the previous level would be worse than having no history at all.

- [ ] **Step 4: Show out-of-bounds walls in the world**

In `EditorScene::render`, before the handles block, so a problem is visible where the wall is rather than only in a list:

```cpp
    // Tint every wall validation rejects, so the reason saving is blocked is
    // visible in the world and not only in a panel.
    for (const logic::Problem& problem : logic::validate(m_state.level)) {
        if (problem.wallIndex < m_state.level.walls.size()) {
            gfx::renderWall(m_state.level.walls[problem.wallIndex], batch, atlas, {1.0f, 0.25f, 0.2f, 0.55f});
        }
    }
```

Add `#include "logic/LevelValidation.hpp"`.

- [ ] **Step 5: Register, build, verify**

Add `src/logic/LevelFiles.cpp` to `CMakeLists.txt`. Then:

- The Files panel lists `default` (the level shipped in Task 3). Click it: the editor loads five walls and two markers, and Undo is greyed out.
- Drag a wall so it hangs outside the level bounds. It turns **red** in the world, the Problems list gains "Wall N: Wall lies outside the level bounds", and the **Save button greys out**. Clicking the problem selects the offending wall. Drag it back inside and Save re-enables.
- Type a name and Save. The file appears in the Open list. Quit, relaunch, open it: every wall's kind, position, rotation, size and colour survive the round trip, as do both markers' edges and spans.
- Check the file is readable: `cat build/linux-debug/bin/assets/levels/<name>.level.json` — rotations in whole-ish degrees, colours as 0-255 triples.
- Type a name containing `../` and save: the file lands in the levels directory with the separators stripped, not outside it.

- [ ] **Step 6: Commit**

```bash
git add src/logic/LevelFiles.hpp src/logic/LevelFiles.cpp src/editor/EditorUi.cpp src/scene/EditorScene.cpp CMakeLists.txt
git commit -m "Add level saving, loading and the validation panel

An in-app list of the levels that actually exist, rather than a native
dialog: levels live in one known directory that ships with the build, and
this works identically on Linux, Windows and macOS with no portal or
threading caveats.

Saving is blocked while any wall is out of bounds or degenerate, with the
offenders tinted red in the world as well as listed. Opening or starting
a new level clears the undo stack, since history from another level is
not history of this one."
```

---

## Task 14: Choosing a level from the main menu

**Files:**
- Modify: `src/scene/MainMenuScene.hpp`, `src/scene/MainMenuScene.cpp`

**Interfaces:**
- Consumes: `logic::listLevels`, `logic::levelDisplayName` (Task 13), `scene::LevelScene(std::filesystem::path)` (Task 4).
- Produces: nothing further.

- [ ] **Step 1: Add picker state to `MainMenuScene.hpp`**

```cpp
private:
    Services* m_services = nullptr;
    gfx::Camera2D m_camera;

    // Levels are only offered as a choice once there is more than one, so the
    // extra click appears only when it has been earned.
    bool m_choosingLevel = false;
```

- [ ] **Step 2: Rework `MainMenuScene::debugUi`**

```cpp
void MainMenuScene::debugUi() {
    ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("horde");

    if (ImGui::Button("Play", ImVec2(160.0f, 0.0f))) {
        // One level means there is nothing to choose between: go straight in.
        if (logic::listLevels().size() > 1) {
            m_choosingLevel = true;
        } else {
            m_services->scenes->push(std::make_unique<LevelScene>());
        }
    }

    if (ImGui::Button("Upgrades", ImVec2(160.0f, 0.0f))) {
        m_services->scenes->push(std::make_unique<TechTreeScene>());
    }

    if (ImGui::Button("Level Editor", ImVec2(160.0f, 0.0f))) {
        m_services->scenes->push(std::make_unique<EditorScene>());
    }

    if (m_choosingLevel) {
        ImGui::SeparatorText("Choose a level");

        for (const std::filesystem::path& path : logic::listLevels()) {
            const std::string name = logic::levelDisplayName(path);
            if (ImGui::Button(name.c_str(), ImVec2(160.0f, 0.0f))) {
                m_choosingLevel = false;
                m_services->scenes->push(std::make_unique<LevelScene>(path));
            }
        }

        if (ImGui::Button("Cancel", ImVec2(160.0f, 0.0f))) {
            m_choosingLevel = false;
        }
    }

    ImGui::End();
}
```

Add `#include <filesystem>`, `#include <string>` and `#include "logic/LevelFiles.hpp"` to `MainMenuScene.cpp`.

- [ ] **Step 3: Build and verify the whole feature end to end**

```bash
clang-format -i $(git ls-files 'src/*.cpp' 'src/*.hpp')
cmake --build --preset linux-debug 2>&1 | tail -20
./build/linux-debug/bin/horde
```

Walk the whole thing:
1. With only `default` present, **Play** enters it directly, showing the five walls and two markers with units bouncing around.
2. Back to the menu, open **Level Editor**, build a small level: a rectangle, a rotated triangle, a circle, an open polyline, a recoloured wall, a second spawn. Save it as `arena`.
3. Back to the menu. **Play** now offers a choice of `arena` and `default`. Pick `arena`: it renders exactly as the editor drew it.
4. Reopen the editor, open `arena`, move something, Ctrl+Z, confirm it reverts.
5. Confirm no warnings appeared in the build output at any point.

- [ ] **Step 4: Commit**

```bash
git add src/scene/MainMenuScene.hpp src/scene/MainMenuScene.cpp
git commit -m "Offer a level choice from the menu when more than one exists

Play enters the only level directly when there is just one, and presents
a list once the editor has produced others, so the extra click appears
only when it has been earned."
```

---

## Done

At this point the spec is fully implemented: a level editor reachable from the menu that places, moves, resizes, rotates, recolours and deletes walls of four kinds; places and stretches edge-constrained spawn and exit markers; snaps position and rotation independently and optionally; undoes and redoes; validates; and saves and loads JSON levels that the game itself renders through the same code path.

Deliberately not built, each with the data model already leaving room for it: per-vertex polyline editing, native OS file dialogs, wall reordering, wall translucency, spawn-to-exit pairing, and any automated tests.
