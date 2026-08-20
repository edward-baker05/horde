#pragma once

#include <glm/vec2.hpp>

#include <vector>

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

// A polyline being drawn. Points are in WORLD space while drafting; they are
// converted to the wall's local space when the draft is committed.
//
// Deliberately a separate type from Placement: a polyline is a click-per-point
// gesture, not a drag, and conflating the two would make both harder to follow.
struct PolylineDraft {
    bool active = false;
    std::vector<glm::vec2> points;
    glm::vec2 cursor{0.0f, 0.0f}; // where the next point would land

    void clear() {
        active = false;
        points.clear();
    }
};

// Commits a draft to a wall, converting its world-space points into local space
// around their centroid. Returns false if the draft has fewer than two points,
// which is not a polyline.
bool finishPolyline(const PolylineDraft& draft, logic::Rgb color, float thickness, logic::Wall& out);

} // namespace horde::editor
