#pragma once

#include <glm/vec2.hpp>

#include "editor/EditorState.hpp"
#include "logic/Level.hpp"

namespace horde::editor {

// A drag smaller than this in either axis is treated as a mis-click rather than
// a wall, which is what stops the editor producing degenerate geometry that
// validation would then refuse to save.
inline constexpr float kMinimumDragExtent = 4.0f;

// A click-drag in progress. `start` is where the button went down, `current`
// follows the cursor.
struct Placement {
    bool active = false;
    glm::vec2 start{0.0f, 0.0f};
    glm::vec2 current{0.0f, 0.0f};
};

// True for the tools whose placement gesture sweeps a bounding box: rectangle,
// triangle and circle. Polyline and the two marker tools do not.
bool isBoxTool(Tool tool);

const char* toolName(Tool tool);

// Builds a wall from a swept box. Returns false if the drag was too small to be
// a deliberate placement, or if `tool` is not a box tool.
//
// A circle takes its radius from half the SMALLER of the box's two extents, so
// that it always fits inside the box the user swept.
bool makeWallFromDrag(Tool tool, glm::vec2 start, glm::vec2 end, logic::Rgb color, logic::Wall& out);

} // namespace horde::editor
