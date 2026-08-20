#include "editor/Markers.hpp"

#include <algorithm>
#include <cmath>

namespace horde::editor {
namespace {

bool runsAlongX(logic::Edge edge) {
    return edge == logic::Edge::North || edge == logic::Edge::South;
}

} // namespace

logic::Edge nearestEdge(const logic::Level& level, glm::vec2 world) {
    const float toNorth = std::abs(world.y);
    const float toSouth = std::abs(level.size.y - world.y);
    const float toWest = std::abs(world.x);
    const float toEast = std::abs(level.size.x - world.x);

    float best = toNorth;
    logic::Edge edge = logic::Edge::North;

    if (toSouth < best) {
        best = toSouth;
        edge = logic::Edge::South;
    }
    if (toWest < best) {
        best = toWest;
        edge = logic::Edge::West;
    }
    if (toEast < best) {
        best = toEast;
        edge = logic::Edge::East;
    }

    return edge;
}

float projectOntoEdge(const logic::Level& level, logic::Edge edge, glm::vec2 world) {
    const float along = runsAlongX(edge) ? world.x : world.y;
    return std::clamp(along, 0.0f, logic::edgeLength(level, edge));
}

void freeSpan(const logic::Level& level, logic::Edge edge, float at, std::size_t ignoreIndex, float& outLow,
              float& outHigh) {
    outLow = 0.0f;
    outHigh = logic::edgeLength(level, edge);

    for (std::size_t i = 0; i < level.markers.size(); ++i) {
        if (i == ignoreIndex) {
            continue;
        }
        const logic::Marker& other = level.markers[i];
        if (other.edge != edge) {
            continue; // each edge is its own interval space; corners never clash
        }

        const float start = other.offset;
        const float end = other.offset + other.length;

        if (end <= at) {
            outLow = std::max(outLow, end);
        } else if (start >= at) {
            outHigh = std::min(outHigh, start);
        } else {
            // `at` is inside another marker: there is no free room here.
            outLow = at;
            outHigh = at;
            return;
        }
    }
}

void clampMarker(logic::Level& level, std::size_t index) {
    if (index >= level.markers.size()) {
        return;
    }

    logic::Marker& marker = level.markers[index];
    const float edge = logic::edgeLength(level, marker.edge);

    // Anchor the clamp on the marker's own midpoint so it keeps its place
    // rather than jumping to an unrelated gap.
    const float midpoint = std::clamp(marker.offset + marker.length * 0.5f, 0.0f, edge);

    float low = 0.0f;
    float high = edge;
    freeSpan(level, marker.edge, midpoint, index, low, high);

    const float available = high - low;
    marker.length = std::clamp(marker.length, std::min(kMinimumMarkerLength, available), std::max(available, 0.0f));
    marker.offset = std::clamp(marker.offset, low, std::max(low, high - marker.length));
}

void clampAllMarkers(logic::Level& level) {
    for (std::size_t i = 0; i < level.markers.size(); ++i) {
        clampMarker(level, i);
    }
}

bool makeMarkerFromDrag(const logic::Level& level, logic::MarkerKind kind, glm::vec2 start, glm::vec2 end,
                        logic::Marker& out) {
    const logic::Edge edge = nearestEdge(level, start);
    const float a = projectOntoEdge(level, edge, start);
    const float b = projectOntoEdge(level, edge, end);

    float low = 0.0f;
    float high = logic::edgeLength(level, edge);
    freeSpan(level, edge, a, level.markers.size(), low, high);

    const float from = std::clamp(std::min(a, b), low, high);
    const float to = std::clamp(std::max(a, b), low, high);

    if (to - from < kMinimumMarkerLength) {
        return false;
    }

    out = logic::Marker{};
    out.kind = kind;
    out.edge = edge;
    out.offset = from;
    out.length = to - from;
    return true;
}

void markerEndHandles(const logic::Level& level, const logic::Marker& marker, glm::vec2& outLow, glm::vec2& outHigh) {
    glm::vec2 center{};
    glm::vec2 size{};
    logic::markerRect(level, marker, center, size);

    if (runsAlongX(marker.edge)) {
        outLow = {marker.offset, center.y};
        outHigh = {marker.offset + marker.length, center.y};
    } else {
        outLow = {center.x, marker.offset};
        outHigh = {center.x, marker.offset + marker.length};
    }
}

} // namespace horde::editor
