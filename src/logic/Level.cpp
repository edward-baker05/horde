#include "logic/Level.hpp"

#include <algorithm>
#include <cmath>

namespace horde::logic {

Level makeDefaultLevel() {
    Level level;

    // Each gate spans its whole side: the horde enters along the full west
    // edge and leaves along the full east edge.
    Marker spawn;
    spawn.kind = MarkerKind::Spawn;
    spawn.edge = Edge::West;
    spawn.offset = 0.0f;
    spawn.length = edgeLength(level, Edge::West);

    Marker exit;
    exit.kind = MarkerKind::Exit;
    exit.edge = Edge::East;
    exit.offset = 0.0f;
    exit.length = edgeLength(level, Edge::East);

    level.markers.push_back(spawn);
    level.markers.push_back(exit);
    resizeWallGrid(level.wallGrid, level.size);
    return level;
}

std::array<Rgb, kPaletteSize> WallGrid::defaultPalette() {
    // Entry 0 is the placed-wall grey, so a grid painted with the first colour
    // matches the rest of the level. The others are picked to stay readable on
    // the default background.
    return {{
        {180, 180, 190},
        {110, 116, 130},
        {196, 92, 82},
        {214, 160, 76},
        {120, 176, 96},
        {80, 156, 188},
        {140, 110, 196},
        {226, 222, 208},
    }};
}

glm::ivec2 wallGridDimFor(glm::vec2 size) {
    return {std::max(0, static_cast<int>(std::ceil(size.x / kCellSize))),
            std::max(0, static_cast<int>(std::ceil(size.y / kCellSize)))};
}

void resizeWallGrid(WallGrid& grid, glm::vec2 size) {
    const glm::ivec2 dim = wallGridDimFor(size);
    if (dim == grid.dim && grid.cells.size() == static_cast<std::size_t>(dim.x) * dim.y) {
        return;
    }

    std::vector<Cell> cells(static_cast<std::size_t>(dim.x) * dim.y);
    for (int y = 0; y < std::min(dim.y, grid.dim.y); ++y) {
        for (int x = 0; x < std::min(dim.x, grid.dim.x); ++x) {
            cells[static_cast<std::size_t>(x) + static_cast<std::size_t>(y) * dim.x] = grid.at({x, y});
        }
    }
    grid.dim = dim;
    grid.cells = std::move(cells);
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
