#pragma once

#include <glm/vec2.hpp>

#include <array>
#include <cstddef>
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

// A box that redirects units passing through it, so a level can steer the
// horde around walls instead of leaving it to collide with them. Not a wall:
// units pass through it freely.
//
// The flow is along the region's local +x axis, so `rotation` sets its
// direction (0 flows toward +x, east). While a unit's centre is inside, it
// stops heading for the exit and turns to follow the flow at full speed;
// `strength` is how quickly it turns, per second (like the movement steering
// rate: higher snaps round sooner).
struct FieldRegion {
    glm::vec2 center{0.0f, 0.0f};
    float rotation = 0.0f; // radians; positive is clockwise on screen
    glm::vec2 halfExtents{50.0f, 50.0f};
    float strength = 3.0f;
};

// Side of one wall-grid cell, in world units: one default unit diameter
// (SimConfig::unitSize), so a one-cell gap is exactly wide enough for a unit.
inline constexpr float kCellSize = 5.0f;

// The geometry of one wall-grid cell. A slope is a right-angled triangle
// filling half the cell; it is named for the corner its right angle sits in,
// so SlopeNW is solid along the cell's north and west sides.
enum class CellShape : std::uint8_t { Empty, Full, SlopeNW, SlopeNE, SlopeSE, SlopeSW };

inline constexpr std::uint8_t kCellShapeCount = 6;

// How many colours a wall grid's palette holds: one cell's colour is a 3-bit
// index into it.
inline constexpr std::size_t kPaletteSize = 8;

// One wall-grid cell, packed into a byte: bits 0-2 are the CellShape, bits
// 3-5 the palette index, bits 6-7 unused and always zero. An empty cell's
// colour bits are always zero too, so every empty cell is the same byte and
// runs of them encode well.
struct Cell {
    std::uint8_t bits = 0;

    CellShape shape() const {
        return static_cast<CellShape>(bits & 0x7u);
    }

    std::uint8_t colorIndex() const {
        return static_cast<std::uint8_t>((bits >> 3) & 0x7u);
    }

    bool empty() const {
        return bits == 0;
    }

    static Cell make(CellShape shape, std::uint8_t colorIndex) {
        if (shape == CellShape::Empty) {
            return Cell{};
        }
        return Cell{static_cast<std::uint8_t>(static_cast<std::uint8_t>(shape) | ((colorIndex & 0x7u) << 3))};
    }

    bool operator==(const Cell& other) const = default;
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
    std::array<Rgb, kPaletteSize> palette = defaultPalette();

    static std::array<Rgb, kPaletteSize> defaultPalette();

    bool contains(glm::ivec2 c) const {
        return c.x >= 0 && c.y >= 0 && c.x < dim.x && c.y < dim.y;
    }

    // Empty outside the grid.
    Cell at(glm::ivec2 c) const {
        return contains(c) ? cells[static_cast<std::size_t>(c.x) + static_cast<std::size_t>(c.y) * dim.x] : Cell{};
    }

    // Ignored outside the grid.
    void set(glm::ivec2 c, Cell value) {
        if (contains(c)) {
            cells[static_cast<std::size_t>(c.x) + static_cast<std::size_t>(c.y) * dim.x] = value;
        }
    }
};

// How many cells it takes to cover a level of `size`, rounded up.
glm::ivec2 wallGridDimFor(glm::vec2 size);

// Resizes the grid to cover a level of `size`, anchored at the north-west
// corner: existing cells keep their coordinates, new ones are empty, and those
// now past the edge are dropped.
void resizeWallGrid(WallGrid& grid, glm::vec2 size);

// A rectangular arena and everything placed in it.
//
// Bounds run from (0,0) to `size`. The background exactly fills those bounds;
// it and field regions are the elements units do not collide with.
struct Level {
    glm::vec2 size{2000.0f, 2000.0f};
    // glm::vec2 size{20.0f, 40.0f};
    Rgb backgroundColor{38, 42, 50};
    std::vector<Wall> walls;     // a later index draws on top of an earlier one
    std::vector<Marker> markers; // always at least one spawn and one exit
    std::vector<FieldRegion> fieldRegions;
    WallGrid wallGrid; // sized to `size` by makeDefaultLevel and loadLevel
};

// Length of one edge of a level, in world units.
inline float edgeLength(const Level& level, Edge edge) {
    return (edge == Edge::North || edge == Edge::South) ? level.size.x : level.size.y;
}

// A blank level: default size, a spawn along the whole west edge and an exit
// along the whole east edge.
Level makeDefaultLevel();

// The axis-aligned world-space rectangle a marker occupies, as centre and size.
// Markers are drawn inward from their edge.
void markerRect(const Level& level, const Marker& marker, glm::vec2& outCenter, glm::vec2& outSize);

// How many markers of this kind the level has. The last spawn and the last exit
// cannot be deleted.
std::size_t countMarkers(const Level& level, MarkerKind kind);

} // namespace horde::logic
