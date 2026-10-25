#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

#include "logic/UnitData.hpp"

namespace horde::bench {

// How well a set of units satisfies "no two units overlap". Speed alone would
// rank a system that ignores collisions first; this is the other half.
struct CollisionQuality {
    bool measured = false;
    std::uint64_t overlappingPairs = 0; // pairs closer than a diameter, beyond a 1% tolerance
    std::uint32_t overlappingUnits = 0; // units in at least one such pair
    double meanPenetration = 0.0;       // over overlapping pairs, as a fraction of the diameter
    double maxPenetration = 0.0;
    // Fraction of the level the units' discs would cover. Above ~0.9 (hexagonal
    // packing) the units cannot all fit without overlapping.
    double density = 0.0;
};

// Exact, via a CPU uniform grid. O(n) for evenly spread units.
CollisionQuality measureCollisions(const std::vector<BaseUnitData>& units, float diameter, glm::vec2 worldSize);

} // namespace horde::bench
