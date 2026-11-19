# Painted walls — design

**Date:** 2026-11-02
**Status:** implemented

A second way to author walls: the level is overlaid with a grid of unit-sized
cells, and the editor gets a brush that paints cells solid. Painting is quick
for organic or blocky layouts that would take dozens of hand-placed rectangles,
and 45° slopes keep it from looking like everything is made of squares.
Painted cells collide exactly like placed walls, because they are flattened
into the same convex primitives.

Vocabulary is defined in `CONTEXT.md`; this adds **cell**, **wall grid** and
**slope** to it (section 8).

---

## Scope

**In:** a per-level wall grid saved in the level file, each cell coloured from
an eight-entry per-level palette; a paint tool with paint,
erase and slope modes, circle and square brushes, and adjustable size;
automatic smoothing of staircase steps into 45° slopes; undo of whole strokes;
converting the grid into wall primitives for collision; drawing it in the
editor and in `LevelScene`.

**Out, deliberately:** slopes other than 45° (2:1 slopes would need two-cell
shapes); more than eight colours per level (the two spare bits per cell could
make it 32 later); converting painted cells
into placed walls or the other way round; any change to how units steer.

Placed walls are unchanged and coexist with painted cells.

---

## 1. Data model

```cpp
// logic/Level.hpp

// Side of one wall-grid cell, in world units: one default unit diameter
// (SimConfig::unitSize), so a one-cell gap is exactly wide enough for a unit.
inline constexpr float kCellSize = 5.0f;

// The geometry of one wall-grid cell. A slope is a right-angled triangle
// filling half the cell; it is named for the corner its right angle sits in,
// so SlopeNW is solid along the cell's north and west sides.
enum class CellShape : std::uint8_t { Empty, Full, SlopeNW, SlopeNE, SlopeSE, SlopeSW };

// How many colours a wall grid's palette holds: one cell's colour is a 3-bit
// index into it.
inline constexpr std::size_t kPaletteSize = 8;

// One wall-grid cell, packed into a byte: bits 0-2 are the CellShape, bits
// 3-5 the palette index, bits 6-7 unused and always zero. An empty cell's
// colour bits are always zero too, so every empty cell is the same byte and
// runs of them encode well.
struct Cell {
    std::uint8_t bits = 0;

    CellShape shape() const;
    std::uint8_t colorIndex() const; // 0 .. kPaletteSize - 1
    static Cell make(CellShape shape, std::uint8_t colorIndex);
};
static_assert(sizeof(Cell) == 1);

// The level overlaid with square cells, each empty, solid or half solid.
// Row-major, row 0 along the north edge. Dimensions follow the level's size,
// rounded up, so the last row and column may hang past the bounds; collision
// and drawing clip nothing, and the overhang is outside where units can go.
//
// Cells store a palette index rather than a colour. Editing a palette entry
// recolours every cell that uses it.
struct WallGrid {
    glm::ivec2 dim{0, 0};
    std::vector<Cell> cells; // dim.x * dim.y
    std::array<Rgb, kPaletteSize> palette; // defaults: see section 2

    Cell at(glm::ivec2 c) const;          // empty outside the grid
    void set(glm::ivec2 c, Cell value);   // ignored outside the grid
};

struct Level {
    ...
    WallGrid wallGrid;
};
```

A 2000×2000 level is 400×400 = 160 000 cells. Shape and colour together fit
in one byte, so a cell costs the same as it would without colour. Whole-level undo
snapshots (`UndoStack::kMaxDepth` = 64) therefore cost at most ~10 MB, so the
existing snapshot undo is kept rather than adding stroke diffs.

Resizing a level resizes the grid anchored at the north-west corner: existing
cells keep their indices, new ones are empty, cut ones are dropped.

The cell size is fixed rather than per level. If `unitSize` ever becomes a
per-level or per-preset setting, this is the constant to revisit.

## 2. File format

The level `version` goes from 1 to 2. Version 1 files load with an empty grid
sized to the level. The grid is saved run-length encoded, since most of it is
empty:

```json
"wallGrid": {
  "dim": [400, 400],
  "palette": [[180, 180, 190], [120, 124, 136], ...],
  "runs": [0, 12034, 1, 40, 10, 12, 0, 359, ...]
}
```

`palette` is exactly eight colours, by index. `runs` is flat pairs of (packed
cell byte, count) in row-major order, so a change of colour starts a new run
just as a change of shape does. On load, each of these is a load error, since
it means the file was edited by hand:
- a `dim` that disagrees with the level's size;
- runs that do not sum to `dim.x * dim.y`;
- a palette that isn't eight entries;
- an unknown shape, set unused bits, or an empty cell with colour bits set.

A level with no `wallGrid` (every version 1 file) gets the default palette:
the existing wall grey, followed by seven colours chosen to read well against
the default background.

## 3. Collision

`WallColliders::build` flattens the grid alongside the placed walls, into the
existing primitive kinds. The shaders don't change.

- **Full cells** are greedily merged into rectangles: scan each row for runs of
  `Full`, then grow each run downward while the row below has the identical
  run unclaimed. Each rectangle becomes one `Box` primitive. Collision ignores
  colour, so a block painted in several colours still merges as one.
- **Slope cells** become one `Triangle` primitive each, from the three corners
  of their half.

Merging keeps the primitive count close to the number of edges rather than the
number of cells. A solid 400×400 block is one box, and a 45° diagonal across a
2000-unit level is ~400 triangles plus its boxes.

Seams between neighbouring primitives are flush, and a unit's centre stays
outside walls, so the per-primitive signed distance gives the right normal at
a seam. A unit sliding along a run of slopes meets a straight surface, because
each triangle's hypotenuse lines up with the next one's.

Worst case, a checkerboard, is one primitive per cell (160k, 5 MB). That is
still correct, because primitives are binned. It is not worth guarding against.

## 4. Painting

A new `Tool::Paint` with three modes, chosen in the editor panel and by
hotkey:

| mode | left drag | right drag |
|---|---|---|
| paint | cells under the brush → `Full`, current colour | erase |
| erase | cells under the brush → `Empty` | paint |
| slope | click a cell: cycle Empty → each slope → Empty, current colour | set `Empty` |

- **Colour:** the panel shows the eight palette entries as swatches. Clicking
  a swatch makes it the current colour; Shift+1–8 do the same while the paint
  tool is active (plain 1–9 still switch tools, 9 being the paint tool).
  `[` and `]` shrink and grow the brush. Double-clicking a swatch opens a colour picker for that
  entry. Changing an entry recolours every cell using it, and is one undo
  step. Painting over solid cells recolours them as well as filling them, so
  the brush doubles as a recolour tool. Alt-click picks the colour under the
  cursor.

- **Brush:** circle or square, 1–64 cells across. A cell is under the brush
  when its centre is. A hover outline shows the brush.
- **Strokes:** the brush is stamped along the segment between consecutive
  mouse samples, so a fast drag leaves no gaps. Shift-click paints a straight
  line from the last stamp. Each stroke calls `beginMutation()` once, at
  button-down, so it undoes as a whole.
- **Auto-smooth** (toggle, default on): when a stroke ends, every empty cell
  within one cell of what the stroke touched is checked. If two adjacent sides
  of it are solid and the other two are empty, it becomes the slope that fills
  that corner. For example, solid W and S with empty N and E gives `SlopeSW`.
  The rule only fills notches and never cuts a corner. A brushed staircase
  therefore becomes a straight 45° surface, and square corners stay sharp. A
  "side is solid" test looks at the neighbour's facing edge, so a slope can
  support another slope and a diagonal run fills in one pass. A filled slope
  takes the current colour if either supporting neighbour has it, and
  otherwise the colour of its north or south neighbour, so slopes match the
  surface they continue rather than the brush. Smoothing follows paint strokes
  only; after an erase it would put back part of what was just removed.

  A concave corner where two straight walls meet also gets a single
  one-cell fillet. That's 5 units, barely visible, and it helps the horde get
  round. Slope mode removes it if it isn't wanted.
- **Slope mode** is for the cases auto-smooth gets wrong, and for drawing
  slopes freehand.

The select tool ignores painted cells; they are edited only with the paint
tool.

## 5. Rendering

`renderLevel` draws the grid right after the background and before placed
walls, as merged rectangles and triangles from the same function collision
uses. Drawing needs one extra rule: it merges full cells only when they share
a palette index. The drawn pieces may be split differently from the
collision ones, but both cover exactly the same cells, so what you see is
still exactly what units hit. A single-colour grid draws with the same shapes
it collides with. Rects
go through the existing rectangle sprite. A slope is a new atlas cell, (3,0):
a right triangle filling the cell's south-west half. It is drawn over the
whole cell and turned in quarter turns. Its straight sides are then the quad's
own edges, so slopes sit flush against their neighbours, and only the diagonal
is antialiased. The isoceles wall triangle, turned 45°, was tried first. It
left one-pixel gaps along every side touching a full cell, because those sides
ran diagonally through its texture.

The editor keeps the merged list and a copy of the cells it came from, and
re-merges the whole grid only on a frame where the cells differ. Comparing
160k bytes is cheap, and catches undo, redo and loading as well as strokes.
`LevelScene` merges once, on entry. If a whole-grid re-merge ever shows up
while painting large levels, the next step is to merge only the rows a stroke
touched.

While the paint tool is active, the editor draws faint cell lines when zoomed in far enough for them to be more than a
few pixels apart.

## 6. Interaction with the rest of a level

- **Spawning** already resolves units out of wall primitives, so painted cells
  need no special case.
- **Markers:** painting over a spawn or exit band is allowed but blocks it.
  Validation reports it as a warning, not as something that stops saving.
- **Field regions** are unaffected; units pass through them over painted cells
  as over anything else.

## 7. Testing

The same as the level editor: by hand in the editor. Two checks go into
`bench/Quality`, which already counts units inside walls:
- A level with a painted staircase and diagonal: no units should stay
  embedded.
- A round trip of save/load on a painted level should come back cell-for-cell
  identical.

## 8. Glossary additions (`CONTEXT.md`)

**Wall grid**: the level overlaid with square cells one unit across, any of
which may be painted solid. Every level has exactly one, though it may be
empty.

**Cell**: one square of the wall grid: empty, full, or a slope, and when not
empty, one of its palette's colours.

**Palette**: the eight colours a level's wall grid can use. Cells refer to an
entry rather than holding a colour, so editing an entry recolours every cell
that uses it.

**Slope**: a cell solid on one side of its diagonal, giving a 45° surface.

Painted cells are walls in the glossary's sense: units collide with them.

## Open questions

- Should auto-smooth also run once over the whole grid on demand, as a "smooth
  all" button, for levels painted with it off?
