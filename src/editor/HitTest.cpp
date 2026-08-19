#include "editor/HitTest.hpp"

#include <algorithm>
#include <cmath>

#include "logic/LevelGeometry.hpp"

namespace horde::editor {
namespace {

// Shortest distance from `p` to the segment ab.
float distanceToSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 ab = b - a;
    const float lengthSquared = ab.x * ab.x + ab.y * ab.y;
    if (lengthSquared <= 0.0f) {
        const glm::vec2 d = p - a;
        return std::sqrt(d.x * d.x + d.y * d.y);
    }

    float t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lengthSquared;
    t = std::clamp(t, 0.0f, 1.0f);

    const glm::vec2 d = p - (a + ab * t);
    return std::sqrt(d.x * d.x + d.y * d.y);
}

} // namespace

bool hitTestWall(const logic::Wall& wall, glm::vec2 world) {
    const glm::vec2 local = logic::worldToLocal(wall, world);

    if (const auto* rect = std::get_if<logic::RectangleShape>(&wall.shape)) {
        return std::abs(local.x) <= rect->halfExtents.x && std::abs(local.y) <= rect->halfExtents.y;
    }

    if (const auto* tri = std::get_if<logic::TriangleShape>(&wall.shape)) {
        // Apex at (0, -hy), base from (-hx, +hy) to (+hx, +hy). The half-width
        // grows linearly from 0 at the apex to hx at the base, which mirrors
        // exactly how the atlas cell is drawn.
        if (local.y < -tri->halfExtents.y || local.y > tri->halfExtents.y) {
            return false;
        }
        const float t = (local.y + tri->halfExtents.y) / (tri->halfExtents.y * 2.0f);
        return std::abs(local.x) <= tri->halfExtents.x * t;
    }

    if (const auto* circle = std::get_if<logic::CircleShape>(&wall.shape)) {
        return local.x * local.x + local.y * local.y <= circle->radius * circle->radius;
    }

    const auto& line = std::get<logic::PolylineShape>(wall.shape);
    const float reach = std::max(line.thickness * 0.5f, 3.0f); // never harder to click than 3 units
    for (std::size_t i = 0; i + 1 < line.points.size(); ++i) {
        if (distanceToSegment(local, line.points[i], line.points[i + 1]) <= reach) {
            return true;
        }
    }
    return false;
}

bool hitTestMarker(const logic::Level& level, const logic::Marker& marker, glm::vec2 world) {
    glm::vec2 center{};
    glm::vec2 size{};
    logic::markerRect(level, marker, center, size);

    return std::abs(world.x - center.x) <= size.x * 0.5f && std::abs(world.y - center.y) <= size.y * 0.5f;
}

Selection pick(const logic::Level& level, glm::vec2 world) {
    // Walls first, topmost down, so what you click is what you see on top.
    for (std::size_t i = level.walls.size(); i-- > 0;) {
        if (hitTestWall(level.walls[i], world)) {
            return Selection{SelectionKind::Wall, i};
        }
    }

    for (std::size_t i = level.markers.size(); i-- > 0;) {
        if (hitTestMarker(level, level.markers[i], world)) {
            return Selection{SelectionKind::Marker, i};
        }
    }

    return Selection{};
}

} // namespace horde::editor
