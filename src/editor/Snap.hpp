#pragma once

#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>

#include <cmath>

#include "editor/EditorState.hpp"

namespace horde::editor {

// Rounds a world position to the grid, if position snapping is on.
//
// The grid is anchored at the level origin, and it is a wall's CENTRE that
// snaps — the one point every wall kind has, which keeps this one rule rather
// than four.
inline glm::vec2 snapPosition(glm::vec2 world, const SnapSettings& snap) {
    if (!snap.position || snap.gridSize <= 0.0f) {
        return world;
    }
    return {std::round(world.x / snap.gridSize) * snap.gridSize, std::round(world.y / snap.gridSize) * snap.gridSize};
}

// Rounds an angle in radians to the nearest snap increment, if rotation
// snapping is on.
inline float snapRotation(float radians, const SnapSettings& snap) {
    if (!snap.rotation || snap.rotationDegrees <= 0.0f) {
        return radians;
    }
    const float step = glm::radians(snap.rotationDegrees);
    return std::round(radians / step) * step;
}

} // namespace horde::editor
