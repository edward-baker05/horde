#include "logic/Level.hpp"

namespace horde::logic {

Level makeDefaultLevel() {
    Level level;

    Marker spawn;
    spawn.kind = MarkerKind::Spawn;
    spawn.edge = Edge::West;
    spawn.length = 100.0f;
    spawn.offset = (level.size.y - spawn.length) * 0.5f;

    Marker exit;
    exit.kind = MarkerKind::Exit;
    exit.edge = Edge::East;
    exit.length = 100.0f;
    exit.offset = (level.size.y - exit.length) * 0.5f;

    level.markers.push_back(spawn);
    level.markers.push_back(exit);
    return level;
}

void markerRect(const Level& level, const Marker& marker, glm::vec2& outCenter, glm::vec2& outSize) {
    const float mid = marker.offset + marker.length * 0.5f;
    const float halfThickness = kMarkerThickness * 0.5f;

    switch (marker.edge) {
        case Edge::North:
            outCenter = {mid, halfThickness};
            outSize = {marker.length, kMarkerThickness};
            break;
        case Edge::South:
            outCenter = {mid, level.size.y - halfThickness};
            outSize = {marker.length, kMarkerThickness};
            break;
        case Edge::West:
            outCenter = {halfThickness, mid};
            outSize = {kMarkerThickness, marker.length};
            break;
        case Edge::East:
            outCenter = {level.size.x - halfThickness, mid};
            outSize = {kMarkerThickness, marker.length};
            break;
    }
}

std::size_t countMarkers(const Level& level, MarkerKind kind) {
    std::size_t count = 0;
    for (const Marker& marker : level.markers) {
        if (marker.kind == kind) {
            ++count;
        }
    }
    return count;
}

} // namespace horde::logic
