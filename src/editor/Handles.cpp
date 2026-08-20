#include "editor/Handles.hpp"

#include <algorithm>
#include <cmath>
#include <variant>

#include "logic/LevelGeometry.hpp"

namespace horde::editor {

std::vector<Handle> wallHandles(const logic::Wall& wall) {
    const logic::Aabb local = logic::localWallBounds(wall);

    std::vector<Handle> handles;
    handles.reserve(5);

    const glm::vec2 corners[4] = {
        {local.min.x, local.min.y},
        {local.max.x, local.min.y},
        {local.min.x, local.max.y},
        {local.max.x, local.max.y},
    };

    for (int i = 0; i < 4; ++i) {
        handles.push_back(Handle{HandleKind::ResizeCorner, i, logic::localToWorld(wall, corners[i])});
    }

    // Circles ignore rotation, so they get no rotate handle. Everything else
    // gets one floating above the top edge, clear of the corners.
    if (!std::holds_alternative<logic::CircleShape>(wall.shape)) {
        const glm::vec2 above{(local.min.x + local.max.x) * 0.5f, local.min.y - kRotateHandleGap};
        handles.push_back(Handle{HandleKind::Rotate, 0, logic::localToWorld(wall, above)});
    }

    return handles;
}

Handle pickHandle(const logic::Wall& wall, glm::vec2 world, float pickRadius) {
    Handle best;
    float bestDistance = pickRadius;

    for (const Handle& handle : wallHandles(wall)) {
        const glm::vec2 d = world - handle.world;
        const float distance = std::sqrt(d.x * d.x + d.y * d.y);

        // Strictly-less keeps the first best; rotate is pushed last but sits
        // clear of the corners, so ties in practice do not arise.
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = handle;
        }
    }

    return best;
}

void applyHandleDrag(logic::Wall& wall, const Handle& handle, glm::vec2 world, float rotationGrabOffset) {
    if (handle.kind == HandleKind::Rotate) {
        const glm::vec2 d = world - wall.center;
        wall.rotation = std::atan2(d.y, d.x) - rotationGrabOffset;
        return;
    }

    if (handle.kind != HandleKind::ResizeCorner) {
        return;
    }

    // Resizing happens in local space, where the shape is axis aligned: the
    // dragged corner's distance from the centre IS the new half-extent. The
    // wall stays centred, so the opposite corner moves too — which is the
    // behaviour the inspector's Width/Height fields also produce.
    const glm::vec2 local = logic::worldToLocal(wall, world);
    const glm::vec2 half{std::max(std::abs(local.x), 1.0f), std::max(std::abs(local.y), 1.0f)};

    if (auto* rect = std::get_if<logic::RectangleShape>(&wall.shape)) {
        rect->halfExtents = half;
        return;
    }

    if (auto* tri = std::get_if<logic::TriangleShape>(&wall.shape)) {
        tri->halfExtents = half;
        return;
    }

    if (auto* circle = std::get_if<logic::CircleShape>(&wall.shape)) {
        circle->radius = std::max(half.x, half.y);
        return;
    }

    // A polyline has no extents of its own, so a corner drag scales its points
    // about the centre by the ratio the corner moved.
    auto& line = std::get<logic::PolylineShape>(wall.shape);
    const logic::Aabb bounds = logic::localWallBounds(wall);
    const glm::vec2 current{std::max(std::abs(bounds.min.x), std::abs(bounds.max.x)),
                            std::max(std::abs(bounds.min.y), std::abs(bounds.max.y))};

    if (current.x <= 0.0f || current.y <= 0.0f) {
        return;
    }

    const glm::vec2 scale = half / current;
    for (glm::vec2& point : line.points) {
        point *= scale;
    }
}

} // namespace horde::editor
