#include "editor/Tools.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/PolishMode.hpp"

namespace horde::editor {

bool isBoxTool(Tool tool) {
    return tool == Tool::Rectangle || tool == Tool::Triangle || tool == Tool::Circle || tool == Tool::FieldRegion;
}

const char* toolName(Tool tool) {
    switch (tool) {
        case Tool::Select:
            return tr("Select");
        case Tool::Rectangle:
            return tr("Rectangle");
        case Tool::Triangle:
            return tr("Triangle");
        case Tool::Circle:
            return tr("Circle");
        case Tool::Polyline:
            return tr("Polyline");
        case Tool::Spawn:
            return tr("Spawn");
        case Tool::Exit:
            return tr("Exit");
        case Tool::FieldRegion:
            return tr("Field region");
        case Tool::Paint:
            return tr("Paint cells");
    }
    return tr("Select");
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

bool makeFieldRegionFromDrag(glm::vec2 start, glm::vec2 end, float strength, logic::FieldRegion& out) {
    const glm::vec2 extent{std::abs(end.x - start.x), std::abs(end.y - start.y)};
    if (extent.x < kMinimumDragExtent || extent.y < kMinimumDragExtent) {
        return false;
    }

    out = logic::FieldRegion{};
    out.center = (start + end) * 0.5f;
    out.halfExtents = extent * 0.5f;
    out.strength = strength;
    return true;
}

bool finishPolyline(const PolylineDraft& draft, logic::Rgb color, float thickness, logic::Wall& out) {
    if (draft.points.size() < 2) {
        return false;
    }

    // The wall's centre is the centroid of its points, so that rotating and
    // dragging a polyline behave like every other wall kind.
    glm::vec2 centroid{0.0f, 0.0f};
    for (const glm::vec2& point : draft.points) {
        centroid += point;
    }
    centroid /= static_cast<float>(draft.points.size());

    logic::PolylineShape line;
    line.thickness = thickness;
    line.points.reserve(draft.points.size());
    for (const glm::vec2& point : draft.points) {
        line.points.push_back(point - centroid);
    }

    out = logic::Wall{};
    out.center = centroid;
    out.rotation = 0.0f;
    out.color = color;
    out.shape = std::move(line);
    return true;
}

} // namespace horde::editor
