#include "logic/LevelValidation.hpp"

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

} // namespace horde::logic
