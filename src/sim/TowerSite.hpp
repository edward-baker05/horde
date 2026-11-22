#pragma once

#include <glm/vec2.hpp>

#include "logic/Level.hpp"

namespace horde::sim {

class WallColliders;

// Whether a tower (radius logic::kTowerBodyRadius) may stand at `center`:
// off every spawn and exit band, and at least its radius plus one unit
// diameter (`unitSize`) clear of every wall and of the level's edge, so a tower
// growing in can never pin a unit against a wall and squeeze it through.
//
// `walls` must have been built with a reach of at least kTowerBodyRadius +
// unitSize, or walls that close won't be found.
bool towerSiteClear(const logic::Level& level, const WallColliders& walls, glm::vec2 center, float unitSize);

} // namespace horde::sim
