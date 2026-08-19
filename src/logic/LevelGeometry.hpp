#pragma once

#include <glm/vec2.hpp>

#include "logic/Level.hpp"

namespace horde::logic {

// An axis-aligned bounding box in world space.
struct Aabb {
    glm::vec2 min{0.0f, 0.0f};
    glm::vec2 max{0.0f, 0.0f};

    bool contains(glm::vec2 point) const {
        return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
    }

    // True if this box lies entirely within `other`.
    bool within(const Aabb& other) const {
        return min.x >= other.min.x && min.y >= other.min.y && max.x <= other.max.x && max.y <= other.max.y;
    }
};

// The bounds of a level, as a box.
Aabb levelBounds(const Level& level);

// A wall's untransformed extent, centred on the origin. For a polyline this is
// the box around its points, grown by half the line thickness.
Aabb localWallBounds(const Wall& wall);

// A wall's world-space bounds after rotation: the box around the four rotated
// corners of its local bounds. Conservative for circles and triangles, which is
// correct for the uses here (out-of-bounds validation and broad-phase picking).
Aabb wallAabb(const Wall& wall);

// Transforms a world-space point into a wall's local space, undoing the wall's
// translation and rotation. This is what makes hit-testing and handle dragging
// correct at arbitrary angles: work in local space, where the shape is axis
// aligned.
glm::vec2 worldToLocal(const Wall& wall, glm::vec2 world);

// The inverse of worldToLocal.
glm::vec2 localToWorld(const Wall& wall, glm::vec2 local);

} // namespace horde::logic
