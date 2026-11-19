#include "logic/LevelValidation.hpp"

#include <cmath>
#include <cstdio>
#include <variant>

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

std::vector<std::string> warnings(const Level& level) {
    std::vector<std::string> out;
    const WallGrid& grid = level.wallGrid;

    for (std::size_t i = 0; i < level.markers.size(); ++i) {
        const Marker& marker = level.markers[i];

        // The row or column of cells lying along the marker's band, against
        // its edge: those are the cells units would have to pass through.
        const int first = static_cast<int>(std::floor(marker.offset / kCellSize));
        const int last = static_cast<int>(std::ceil((marker.offset + marker.length) / kCellSize)) - 1;
        const bool horizontal = marker.edge == Edge::North || marker.edge == Edge::South;
        int across = 0; // north and west: the first row or column
        if (marker.edge == Edge::South) {
            across = grid.dim.y - 1;
        } else if (marker.edge == Edge::East) {
            across = grid.dim.x - 1;
        }

        int covered = 0;
        int total = 0;
        for (int along = first; along <= last; ++along) {
            const glm::ivec2 cell = horizontal ? glm::ivec2{along, across} : glm::ivec2{across, along};
            if (!grid.contains(cell)) {
                continue;
            }
            ++total;
            covered += grid.at(cell).empty() ? 0 : 1;
        }

        if (covered > 0) {
            char message[160];
            std::snprintf(message, sizeof(message), "%s %zu: %d of its %d cells are painted over",
                          marker.kind == MarkerKind::Spawn ? "Spawn" : "Exit", i, covered, total);
            out.emplace_back(message);
        }
    }

    return out;
}

} // namespace horde::logic
