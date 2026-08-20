#include "editor/Tools.hpp"

#include <algorithm>
#include <cmath>

namespace horde::editor {

bool isBoxTool(Tool tool) {
    return tool == Tool::Rectangle || tool == Tool::Triangle || tool == Tool::Circle;
}

const char* toolName(Tool tool) {
    switch (tool) {
        case Tool::Select:
            return "Select";
        case Tool::Rectangle:
            return "Rectangle";
        case Tool::Triangle:
            return "Triangle";
        case Tool::Circle:
            return "Circle";
        case Tool::Polyline:
            return "Polyline";
        case Tool::Spawn:
            return "Spawn";
        case Tool::Exit:
            return "Exit";
    }
    return "Select";
}

bool makeWallFromDrag(Tool tool, glm::vec2 start, glm::vec2 end, logic::Rgb color, logic::Wall& out) {
    if (!isBoxTool(tool)) {
        return false;
    }

    const glm::vec2 extent{std::abs(end.x - start.x), std::abs(end.y - start.y)};
    if (extent.x < kMinimumDragExtent || extent.y < kMinimumDragExtent) {
        return false;
    }

    out = logic::Wall{};
    out.center = (start + end) * 0.5f;
    out.rotation = 0.0f;
    out.color = color;

    switch (tool) {
        case Tool::Rectangle:
            out.shape = logic::RectangleShape{extent * 0.5f};
            break;
        case Tool::Triangle:
            out.shape = logic::TriangleShape{extent * 0.5f};
            break;
        case Tool::Circle:
            out.shape = logic::CircleShape{std::min(extent.x, extent.y) * 0.5f};
            break;
        default:
            return false;
    }

    return true;
}

} // namespace horde::editor
