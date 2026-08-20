#include "logic/LevelGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <variant>

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
