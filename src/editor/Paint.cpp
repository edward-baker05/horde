#include "editor/Paint.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace horde::editor {

using logic::Cell;
using logic::CellShape;

void CellBox::include(glm::ivec2 cell) {
    if (!valid) {
        lo = cell;
        hi = cell;
        valid = true;
        return;
    }
    lo = glm::min(lo, cell);
    hi = glm::max(hi, cell);
}

glm::ivec2 cellAt(glm::vec2 world) {
    return glm::ivec2(glm::floor(world / logic::kCellSize));
}

std::vector<glm::ivec2> brushCells(const logic::WallGrid& grid, glm::vec2 world, const PaintSettings& brush) {
    const int size = std::clamp(brush.size, 1, kMaxBrushSize);
    const float radius = static_cast<float>(size) * 0.5f;

    // In cell units: odd sizes centre on a cell centre, even ones on a corner.
    const glm::vec2 point = world / logic::kCellSize;
    const glm::vec2 centre = size % 2 == 1 ? glm::floor(point) + 0.5f : glm::round(point);

    const glm::ivec2 lo = glm::ivec2(glm::floor(centre - radius));
    const glm::ivec2 hi = glm::ivec2(glm::ceil(centre + radius)) - 1;

    std::vector<glm::ivec2> cells;
    for (int y = lo.y; y <= hi.y; ++y) {
        for (int x = lo.x; x <= hi.x; ++x) {
            const glm::ivec2 cell{x, y};
            if (!grid.contains(cell)) {
                continue;
            }
            const glm::vec2 d = glm::vec2(cell) + 0.5f - centre;
            const bool inside = brush.shape == BrushShape::Square ? std::abs(d.x) <= radius && std::abs(d.y) <= radius
                                                                  : glm::dot(d, d) <= radius * radius;
            if (inside) {
                cells.push_back(cell);
            }
        }
    }
    return cells;
}

bool stampBrush(logic::WallGrid& grid, glm::vec2 world, const PaintSettings& brush, Cell value, CellBox& touched) {
    bool changed = false;
    for (const glm::ivec2& cell : brushCells(grid, world, brush)) {
        if (grid.at(cell) != value) {
            grid.set(cell, value);
            changed = true;
        }
        touched.include(cell);
    }
    return changed;
}

bool stampLine(logic::WallGrid& grid, glm::vec2 from, glm::vec2 to, const PaintSettings& brush, Cell value,
               CellBox& touched) {
    // Half a cell apart, so consecutive stamps always overlap.
    const float length = glm::length(to - from);
    const int steps = std::max(1, static_cast<int>(std::ceil(length / (logic::kCellSize * 0.5f))));

    bool changed = false;
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        changed |= stampBrush(grid, from + (to - from) * t, brush, value, touched);
    }
    return changed;
}

namespace {

// Whether the cell's edge facing in each direction is solid along its whole
// length. A slope is solid along the two sides that meet at its right angle.
bool solidNorth(CellShape s) {
    return s == CellShape::Full || s == CellShape::SlopeNW || s == CellShape::SlopeNE;
}

bool solidSouth(CellShape s) {
    return s == CellShape::Full || s == CellShape::SlopeSW || s == CellShape::SlopeSE;
}

bool solidWest(CellShape s) {
    return s == CellShape::Full || s == CellShape::SlopeNW || s == CellShape::SlopeSW;
}

bool solidEast(CellShape s) {
    return s == CellShape::Full || s == CellShape::SlopeNE || s == CellShape::SlopeSE;
}

} // namespace

void autoSmooth(logic::WallGrid& grid, const CellBox& box, std::uint8_t preferredColor) {
    if (!box.valid) {
        return;
    }

    const glm::ivec2 lo = glm::max(box.lo - 1, glm::ivec2(0));
    const glm::ivec2 hi = glm::min(box.hi + 1, grid.dim - 1);

    for (int y = lo.y; y <= hi.y; ++y) {
        for (int x = lo.x; x <= hi.x; ++x) {
            const glm::ivec2 c{x, y};
            if (!grid.at(c).empty()) {
                continue;
            }

            const Cell n = grid.at(c + glm::ivec2(0, -1));
            const Cell s = grid.at(c + glm::ivec2(0, 1));
            const Cell w = grid.at(c + glm::ivec2(-1, 0));
            const Cell e = grid.at(c + glm::ivec2(1, 0));

            // Each neighbour's side that faces this cell.
            const bool north = solidSouth(n.shape());
            const bool south = solidNorth(s.shape());
            const bool west = solidEast(w.shape());
            const bool east = solidWest(e.shape());

            CellShape shape = CellShape::Empty;
            Cell vertical;
            Cell horizontal;
            if (north && west && !south && !east) {
                shape = CellShape::SlopeNW;
                vertical = n;
                horizontal = w;
            } else if (north && east && !south && !west) {
                shape = CellShape::SlopeNE;
                vertical = n;
                horizontal = e;
            } else if (south && east && !north && !west) {
                shape = CellShape::SlopeSE;
                vertical = s;
                horizontal = e;
            } else if (south && west && !north && !east) {
                shape = CellShape::SlopeSW;
                vertical = s;
                horizontal = w;
            } else {
                continue;
            }

            const bool preferred = vertical.colorIndex() == preferredColor || horizontal.colorIndex() == preferredColor;
            grid.set(c, Cell::make(shape, preferred ? preferredColor : vertical.colorIndex()));
        }
    }
}

CellShape nextSlope(CellShape shape) {
    switch (shape) {
        case CellShape::SlopeNW:
            return CellShape::SlopeNE;
        case CellShape::SlopeNE:
            return CellShape::SlopeSE;
        case CellShape::SlopeSE:
            return CellShape::SlopeSW;
        case CellShape::SlopeSW:
            return CellShape::Empty;
        default:
            return CellShape::SlopeNW;
    }
}

} // namespace horde::editor
