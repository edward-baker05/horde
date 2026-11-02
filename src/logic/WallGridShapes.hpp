#pragma once

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>
#include <vector>

#include "logic/Level.hpp"

namespace horde::logic {

// A run of full cells merged into one axis-aligned rectangle, in cells.
struct GridRect {
    glm::ivec2 origin{0, 0}; // north-west cell
    glm::ivec2 size{1, 1};
    std::uint8_t colorIndex = 0;
};

// One slope cell.
struct GridSlope {
    glm::ivec2 cell{0, 0};
    CellShape shape = CellShape::SlopeNW;
    std::uint8_t colorIndex = 0;
};

// A wall grid as the few convex pieces it is made of. Collision and drawing
// both come from this, so what is drawn is exactly what units hit.
struct GridPieces {
    std::vector<GridRect> rects;
    std::vector<GridSlope> slopes;
};

// Greedily merges full cells into rectangles: runs along each row, each grown
// downward while the rows below hold the identical run. Slopes are left one per
// cell. With `byColor` false, colour is ignored (collision); with it true, only
// cells sharing a palette index merge (drawing).
GridPieces mergeWallGrid(const WallGrid& grid, bool byColor);

// World-space corners of a slope cell's solid half, right-angle corner first.
std::array<glm::vec2, 3> slopeCorners(glm::ivec2 cell, CellShape shape);

} // namespace horde::logic
