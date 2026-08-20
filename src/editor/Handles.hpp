#pragma once

#include <glm/vec2.hpp>

#include <vector>

#include "logic/Level.hpp"

namespace horde::editor {

// How far beyond a wall's local bounds the rotate handle floats, in world units.
inline constexpr float kRotateHandleGap = 22.0f;

enum class HandleKind { None, ResizeCorner, Rotate };

// One draggable grip on a selected wall.
//
// `corner` is 0..3 for ResizeCorner, ordered (min,min), (max,min), (min,max),
// (max,max) in the wall's LOCAL space, and unused otherwise.
struct Handle {
    HandleKind kind = HandleKind::None;
    int corner = 0;
    glm::vec2 world{0.0f, 0.0f};
};

// The handles this wall has, in world space.
//
// A wall kind reports its own handles rather than every caller assuming four
// corners. That is what makes per-vertex polyline handles a later addition to
// one function rather than a rewrite of the drag code.
std::vector<Handle> wallHandles(const logic::Wall& wall);

// The handle within `pickRadius` world units of `world`, or one with kind None.
// Rotate wins ties, because it sits outside the shape where nothing else is.
Handle pickHandle(const logic::Wall& wall, glm::vec2 world, float pickRadius);

// Applies a drag of `handle` to `world`, mutating the wall.
//
// `rotationGrabOffset` is the angle between the wall's rotation and the cursor
// at the moment the rotate handle was grabbed; passing it keeps the shape from
// snapping round to meet the cursor on the first frame. It is ignored for
// resize handles.
void applyHandleDrag(logic::Wall& wall, const Handle& handle, glm::vec2 world, float rotationGrabOffset);

} // namespace horde::editor
