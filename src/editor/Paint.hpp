#pragma once

#include <glm/vec2.hpp>

#include <vector>

#include "editor/EditorState.hpp"
#include "logic/Level.hpp"

namespace horde::editor {

// The smallest box of cells holding everything a stroke has touched, so the
// work done when it ends is proportional to the stroke, not the level.
struct CellBox {
    bool valid = false;
    glm::ivec2 lo{0, 0};
    glm::ivec2 hi{0, 0}; // inclusive

    void include(glm::ivec2 cell);
};

// The cell containing a world position. May lie outside the grid.
glm::ivec2 cellAt(glm::vec2 world);

// Every cell the brush covers when centred at `world`, clipped to the grid.
//
// The brush centre snaps so that a brush n cells across covers exactly n cells
// in each direction: to a cell centre for odd sizes, to a cell corner for even
// ones. A cell is under the brush when its centre is.
std::vector<glm::ivec2> brushCells(const logic::WallGrid& grid, glm::vec2 world, const PaintSettings& brush);

// Sets every cell under the brush to `value`. Returns whether anything changed.
bool stampBrush(logic::WallGrid& grid, glm::vec2 world, const PaintSettings& brush, logic::Cell value,
                CellBox& touched);

// Stamps the brush along the segment from `from` to `to`, closely enough that
// a fast drag leaves no gaps. `from` itself is not stamped: it was the end of
// the previous segment. Returns whether anything changed.
bool stampLine(logic::WallGrid& grid, glm::vec2 from, glm::vec2 to, const PaintSettings& brush, logic::Cell value,
               CellBox& touched);

// Fills staircase notches with slopes, in `box` grown by one cell.
//
// An empty cell becomes a slope when two adjacent sides of it are solid and the
// other two are not, so a brushed staircase becomes a straight 45° surface. It
// never cuts a corner off, only fills one in. A "solid side" is the facing edge
// of the neighbour, so an existing slope can support a new one.
//
// The new slope takes `preferredColor` if either supporting neighbour has it,
// and otherwise the colour of the north or south one, so it matches the
// surface it continues rather than the brush.
void autoSmooth(logic::WallGrid& grid, const CellBox& box, std::uint8_t preferredColor);

// The next shape when slope mode is clicked: empty, then each slope in turn
// (NW, NE, SE, SW), then back to empty. A full cell starts the cycle at NW.
logic::CellShape nextSlope(logic::CellShape shape);

} // namespace horde::editor
