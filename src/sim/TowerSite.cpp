#include "sim/TowerSite.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include "logic/TowerStats.hpp"
#include "sim/WallColliders.hpp"

namespace horde::sim {

bool towerSiteClear(const logic::Level& level, const WallColliders& walls, glm::vec2 center, float unitSize) {
    const float radius = logic::kTowerBodyRadius;
    // The level's edge is a wall too: leave a unit's width there as well.
    const float edge = radius + unitSize;
    if (center.x < edge || center.y < edge || center.x > level.size.x - edge || center.y > level.size.y - edge) {
        return false;
    }

    for (const logic::Marker& marker : level.markers) {
        glm::vec2 markerCenter;
        glm::vec2 size;
        logic::markerRect(level, marker, markerCenter, size);
        const glm::vec2 nearest = glm::clamp(center, markerCenter - size * 0.5f, markerCenter + size * 0.5f);
        if (glm::distance(nearest, center) < radius) {
            return false;
        }
    }

    float distance = 0.0f;
    glm::vec2 normal;
    return !walls.nearest(center, distance, normal) || distance >= radius + unitSize;
}

} // namespace horde::sim
