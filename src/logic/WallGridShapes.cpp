#include "logic/WallGridShapes.hpp"

namespace horde::logic {

GridPieces mergeWallGrid(const WallGrid& grid, bool byColor) {
    GridPieces pieces;
    const glm::ivec2 dim = grid.dim;
    std::vector<bool> claimed(grid.cells.size(), false);

    const auto index = [&](int x, int y) { return static_cast<std::size_t>(x) + static_cast<std::size_t>(y) * dim.x; };
    const auto mergeable = [&](int x, int y, std::uint8_t color) {
        const Cell cell = grid.cells[index(x, y)];
        return !claimed[index(x, y)] && cell.shape() == CellShape::Full && (!byColor || cell.colorIndex() == color);
    };

    for (int y = 0; y < dim.y; ++y) {
        for (int x = 0; x < dim.x; ++x) {
            const Cell cell = grid.cells[index(x, y)];
            if (cell.empty() || claimed[index(x, y)]) {
                continue;
            }
            if (cell.shape() != CellShape::Full) {
                pieces.slopes.push_back({{x, y}, cell.shape(), cell.colorIndex()});
                continue;
            }

            const std::uint8_t color = cell.colorIndex();
            int width = 1;
            while (x + width < dim.x && mergeable(x + width, y, color)) {
                ++width;
            }

            // Grow down while the whole run below is mergeable.
            int height = 1;
            for (; y + height < dim.y; ++height) {
                bool rowMatches = true;
                for (int i = 0; i < width && rowMatches; ++i) {
                    rowMatches = mergeable(x + i, y + height, color);
                }
                if (!rowMatches) {
                    break;
                }
            }

            for (int j = 0; j < height; ++j) {
                for (int i = 0; i < width; ++i) {
                    claimed[index(x + i, y + j)] = true;
                }
            }
            pieces.rects.push_back({{x, y}, {width, height}, color});
            x += width - 1;
        }
    }
    return pieces;
}

std::array<glm::vec2, 3> slopeCorners(glm::ivec2 cell, CellShape shape) {
    const glm::vec2 nw = glm::vec2(cell) * kCellSize;
    const glm::vec2 ne = nw + glm::vec2(kCellSize, 0.0f);
    const glm::vec2 se = nw + glm::vec2(kCellSize, kCellSize);
    const glm::vec2 sw = nw + glm::vec2(0.0f, kCellSize);

    switch (shape) {
        case CellShape::SlopeNE:
            return {ne, se, nw};
        case CellShape::SlopeSE:
            return {se, sw, ne};
        case CellShape::SlopeSW:
            return {sw, nw, se};
        default:
            return {nw, ne, sw};
    }
}

} // namespace horde::logic
