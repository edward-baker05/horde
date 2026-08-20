#pragma once

#include <glm/vec2.hpp>

#include "editor/EditorState.hpp"
#include "logic/Level.hpp"

namespace horde::editor {

// True if `world` lies inside this wall.
//
// Works by transforming the point into the wall's local space, where the shape
// is axis aligned and each kind's test is a two-line formula. This is what makes
// picking correct at arbitrary rotations.
bool hitTestWall(const logic::Wall& wall, glm::vec2 world);

// True if `world` lies inside this marker's band.
bool hitTestMarker(const logic::Level& level, const logic::Marker& marker, glm::vec2 world);

// What the cursor is over: the topmost wall (highest index, since a later index
// draws on top), else a marker, else nothing.
Selection pick(const logic::Level& level, glm::vec2 world);

} // namespace horde::editor
